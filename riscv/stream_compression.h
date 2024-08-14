//
// Created by john on 8/3/24.
//

#ifndef _STREAM_COMPRESSION_H
#define _STREAM_COMPRESSION_H

#include <functional>
#include <cstddef>

class stream_compression_t
{
public:
  /**
   * Compress a memory region to a zlib stream.
   *
   * @param src Pointer to the src region
   * @param len Size of the src region
   * @param out_chunk_sz The size for all the chucks except the last one (which may less or equal to the chunk size).
   * @param on_chunk_compressed_cb Callback on a compressed chunk is generated
   * @throw std::runtime_error When error happens.
   */
  static void compress_region(const void *src, size_t len, size_t out_chunk_sz, const std::function<void(const char *data, size_t len)> &on_chunk_compressed_cb);

  /**
   * Decompress a zlib compression stream to a memory region.
   *
   * @param dst Pointer to the destination where the decompressed data should go.
   * @param dst_limit The size of the destination. If the decompressed data is larger than
   *                  this size, function will throw a std::runtime_error with according error message.
   * @param chunk_supplying_cb The callback to supply compressed stream. Will be called when a compressed
   *                           chunk is wanted. If called after the chunk stream is ended, it should
   *                           set the input to nullptr and len to 0.
   * @throw std::runtime_error When error happens.
   * @return Size of decompressed data.
   */

  static size_t decompress_region(void *dst, size_t dst_limit, const std::function<void(char *&input, size_t &len)> &chunk_supplying_cb);
};


#endif //_STREAM_COMPRESSION_H
