// (c) 2013 Maciej Gajewski, <maciej.gajewski0@gmail.com>

#include "coroutines/globals.hpp"

#include "parallel.hpp"
#include "lzma_decompress.hpp"

#include <boost/filesystem.hpp>

#include <iostream>

#include <signal.h>

#include <stdio.h>


using namespace coroutines;
namespace bfs = boost::filesystem;

namespace torture {

static const unsigned BUFFERS = 8;
static const unsigned BUFFER_SIZE = 100*1024;

class file
{
public:

    file(const char* path, const char* mode)
    {
        _f = ::fopen(path, mode);
        if (!_f)
            throw std::runtime_error("Unable to open file");
    }

    ~file()
    {
        ::fclose(_f);
    }

    std::size_t read(void* buf, std::size_t max)
    {
        block();
            std::size_t r = ::fread(buf, 1, max, _f);
        unblock();
        return r;
    }

    std::size_t write(void* buf, std::size_t size)
    {
        block();
            std::size_t r = ::fwrite(buf, size, 1, _f);
        unblock();
        return r;
    }

private:

    FILE* _f = nullptr;
};

void process_file(const bfs::path& in_path, const bfs::path& out_path);
void write_output(buffer_reader& decompressed, buffer_writer& decompressed_return, const bfs::path& output_file);
void read_input(buffer_writer& compressed, buffer_reader& compressed_return, const bfs::path& input_file);

void signal_handler(int)
{
    scheduler * sched = get_scheduler();
    if (sched)
    {
        sched->debug_dump();
    }
}

// Main entry point
void parallel(const char* in, const char* out)
{
    // install signal handler
    signal(SIGINT, signal_handler);

    scheduler sched(4 /*threads*/); // Go: runtime.MAXPROC(4)
    set_scheduler(&sched);

    try
    {
        bfs::path input_dir(in);
        bfs::path output_dir(out);


        for(bfs::directory_iterator it(input_dir); it != bfs::directory_iterator(); ++it)
        {
            if (it->path().extension() == ".xz" && it->status().type() == bfs::regular_file)
            {
                bfs::path output_path = output_dir / it->path().filename().stem();
                go(std::string("process_file ") + it->path().string(), process_file, it->path(), output_path);
            }

        }

    }
    catch(const std::exception& e)
    {
        std::cerr << "Error :" << e.what() << std::endl;
    }

    sched.wait();
    set_scheduler(nullptr);
}

void process_file(const bfs::path& input_file, const bfs::path& output_file)
{
    channel_pair<buffer> compressed = make_channel<buffer>(BUFFERS, "compressed");
    channel_pair<buffer> decompressed = make_channel<buffer>(BUFFERS, "decompressed");
    channel_pair<buffer> compressed_return = make_channel<buffer>(BUFFERS, "compressed_return");
    channel_pair<buffer> decompressed_return = make_channel<buffer>(BUFFERS, "decompressed_return");


    // start writer
    go(std::string("write_output ") + output_file.string(),
        write_output,
        decompressed.reader, decompressed_return.writer, output_file);

    // start decompressor
    go(std::string("lzma_decompress ") + input_file.string(),
        lzma_decompress,
        compressed.reader, compressed_return.writer,
        decompressed_return.reader, decompressed.writer);

    // read (in this coroutine)
    read_input(compressed.writer, compressed_return.reader, input_file);
}

void read_input(buffer_writer& compressed, buffer_reader& compressed_return, const bfs::path& input_file)
{
    try
    {
        file f(input_file.string().c_str(), "rb");

        unsigned counter = 0;
        for(;;)
        {
            buffer b;
            if (counter++ < BUFFERS)
                b = buffer(BUFFER_SIZE);
            else
                b = compressed_return.get(); // get spent buffer from decoder
            std::size_t r = f.read(b.begin(), b.capacity());
            if (r == 0)
                break; // this will close the channel
            else
            {
                b.set_size(r);
                compressed.put(std::move(b));
            }
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << "Error reading file " << input_file << " : " << e.what() << std::endl;
    }
}


void write_output(buffer_reader& decompressed, buffer_writer& decompressed_return, const  bfs::path& output_file)
{
    try
    {
        // open file
        file f(output_file.string().c_str(), "wb");

        // fill the queue with allocated buffers
        for(unsigned i = 0; i < BUFFERS; i++)
        {
            decompressed_return.put(buffer(BUFFER_SIZE));
        }

        for(;;)
        {
            buffer b = decompressed.get();
            f.write(b.begin(), b.size());
            decompressed_return.put_nothrow(std::move(b)); // return buffer to decoder
        }
    }
    catch(const channel_closed&) // this exception is expected when channels are closed
    {
    }
    catch(const std::exception& e)
    {
        std::cerr << "Error writing to output file " << output_file << " : " << e.what() << std::endl;
    }
}

}
