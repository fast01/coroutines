// (c) 2013 Maciej Gajewski, <maciej.gajewski0@gmail.com>

#include "libcoroutines/globals.hpp"

#include "parallel.hpp"
#include "lzma_decompress.hpp"

#include <boost/filesystem.hpp>

#include <iostream>

#include <stdio.h>

using namespace coroutines;
namespace bfs = boost::filesystem;

namespace torture {

static const unsigned BUFFERS = 2;
static const unsigned BUFFER_SIZE = 4*1024;

class file
{
public:

    file(const char* path, const char* mode)
    {
        _f = ::fopen(path, mode);
        if (!_f)
            std::runtime_error("Unable to open file");
    }

    ~file()
    {
        ::fclose(_f);
    }

    std::size_t read(void* buf, std::size_t max)
    {
        return ::fread(buf, 1, max, _f);
    }

    std::size_t write(void* buf, std::size_t size)
    {
        return ::fwrite(buf, 1, size, _f);
    }

private:

    FILE* _f = nullptr;
};


void process_file(const bfs::path& in_path, const bfs::path& out_path);
void write_output(buffer_reader& decompressed, buffer_writer& decompressed_return, const bfs::path& output_file);
void read_input(buffer_writer& compressed, buffer_reader& compressed_return, const bfs::path& input_file);

// Main entry point
void parallel(const char* in, const char* out)
{

    std::unique_ptr<threaded_scheduler> sched(new threaded_scheduler);
    set_scheduler(sched.get());

    // code here
    try
    {
        bfs::path input_dir(in);
        bfs::path output_dir(out);


        for(bfs::directory_iterator it(input_dir); it != bfs::directory_iterator(); ++it)
        {
            if (it->path().extension() == ".xz" && it->status().type() == bfs::regular_file)
            {
                bfs::path output_path = output_dir /= it->path().filename().stem();
                go(process_file, it->path(), output_path);
            }

        }

    }
    catch(const std::exception& e)
    {
        std::cerr << "Error :" << e.what() << std::endl;
    }

    sched->wait();
    set_scheduler(nullptr);
    sched.reset();
}

void process_file(const bfs::path& input_file, const bfs::path& output_file)
{
    std::cout << "process file: " << input_file << " -> " << output_file << std::endl;

    channel_pair<buffer> compressed = make_channel<buffer>(BUFFERS);
    channel_pair<buffer> decompressed = make_channel<buffer>(BUFFERS);
    channel_pair<buffer> compressed_return = make_channel<buffer>(BUFFERS);
    channel_pair<buffer> decompressed_return = make_channel<buffer>(BUFFERS);


    // start writer
    go(write_output, std::move(decompressed.reader), std::move(decompressed_return.writer), output_file);

    // start decompressor
    go(lzma_decompress,
        std::move(compressed.reader), std::move(compressed_return.writer),
        std::move(decompressed_return.reader), std::move(decompressed.writer));

    // read (in this coroutine)
    read_input(compressed.writer, compressed_return.reader, input_file);
}

void read_input(buffer_writer& compressed, buffer_reader& compressed_return, const bfs::path& input_file)
{
    try
    {
        file f(input_file.string().c_str(), "rb");

        for(;;)
        {
            unsigned counter = 0;
            buffer b;
            if (counter++ < BUFFERS)
                b = buffer(BUFFER_SIZE);
            else
                b = compressed_return.get();
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
        std::cerr << "Error processing file " << input_file << " : " << e.what() << std::endl;
    }
}


void write_output(buffer_reader& decompressed, buffer_writer& decompressed_return, const  bfs::path& output_file)
{
    std::cout << "write_output: " << output_file << std::endl;

    try
    {
        // open file
        file f(output_file.string().c_str(), "wb");

        // fill the queue with allocated buffers
        for(int i = 0; i < BUFFERS; i++)
        {
            decompressed_return.put(buffer(BUFFER_SIZE));
        }

        for(;;)
        {
            buffer b = decompressed.get();
            f.write(b.begin(), b.size());
            decompressed_return.put(std::move(b));
        }
    }
    catch(const channel_closed&)
    {
    }
    catch(const std::exception& e)
    {
        std::cout << "Error writing to output file " << output_file << " : " << e.what() << std::endl;
    }
}

}
