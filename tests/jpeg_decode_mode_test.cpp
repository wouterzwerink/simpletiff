// Copyright 2026 Jonas Teuwen. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "simpletiff/io_utils.h"
#include "simpletiff/reader.h"

#if !defined(SIMPLETIFF_JPEG_BACKEND_JPGD)
// Needed on gcc-11 before <jpeglib.h>, which uses the global FILE.
#include <stdio.h>

#include <jpeglib.h>
#endif

namespace simpletiff {
namespace {

TEST(JpegDecodeModeTest, DefaultsToFast) {
  DecodeContext ctx;
  EXPECT_EQ(ctx.jpeg_decode_mode, JpegDecodeMode::kFast);
  EXPECT_TRUE(JpegDecodeModeSupported(JpegDecodeMode::kFast));
}

TEST(JpegDecodeModeTest, MoveKeepsMode) {
  DecodeContext ctx;
  ctx.jpeg_decode_mode = JpegDecodeMode::kLibjpeg;
  DecodeContext moved(std::move(ctx));
  EXPECT_EQ(moved.jpeg_decode_mode, JpegDecodeMode::kLibjpeg);
  DecodeContext assigned;
  assigned = std::move(moved);
  EXPECT_EQ(assigned.jpeg_decode_mode, JpegDecodeMode::kLibjpeg);
}

#if defined(SIMPLETIFF_JPEG_BACKEND_JPGD)

TEST(JpegDecodeModeTest, JpgdOnlySupportsFast) {
  EXPECT_FALSE(JpegDecodeModeSupported(JpegDecodeMode::kLibjpeg));
}

#else

constexpr int kWidth = 64;
constexpr int kHeight = 48;

// A smooth colour gradient with some texture, encoded as YCbCr 4:2:0, the
// layout where both IDCT and chroma upsampling choices change the output.
std::vector<uint8_t> EncodeTestJpeg() {
  std::vector<uint8_t> rgb(static_cast<size_t>(kWidth) * kHeight * 3);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      uint8_t* px = &rgb[(static_cast<size_t>(y) * kWidth + x) * 3];
      px[0] = static_cast<uint8_t>(x * 4);
      px[1] = static_cast<uint8_t>(y * 5);
      px[2] = static_cast<uint8_t>(((x ^ y) & 7) * 32);
    }
  }

  jpeg_compress_struct cinfo{};
  jpeg_error_mgr jerr{};
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  unsigned char* out = nullptr;
  unsigned long out_size = 0;  // NOLINT(runtime/int): libjpeg API type.
  jpeg_mem_dest(&cinfo, &out, &out_size);
  cinfo.image_width = kWidth;
  cinfo.image_height = kHeight;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;
  jpeg_set_defaults(&cinfo);  // YCbCr with 2x2 chroma subsampling.
  jpeg_set_quality(&cinfo, 75, TRUE);
  jpeg_start_compress(&cinfo, TRUE);
  while (cinfo.next_scanline < cinfo.image_height) {
    JSAMPROW row = &rgb[static_cast<size_t>(cinfo.next_scanline) * kWidth * 3];
    jpeg_write_scanlines(&cinfo, &row, 1);
  }
  jpeg_finish_compress(&cinfo);
  std::vector<uint8_t> jpeg(out, out + out_size);
  jpeg_destroy_compress(&cinfo);
  free(out);
  return jpeg;
}

// Decode with libjpeg's defaults, as libtiff and OpenSlide do.
std::vector<uint8_t> ReferenceDecode(const std::vector<uint8_t>& jpeg) {
  jpeg_decompress_struct cinfo{};
  jpeg_error_mgr jerr{};
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);
  jpeg_mem_src(&cinfo, const_cast<uint8_t*>(jpeg.data()), jpeg.size());
  jpeg_read_header(&cinfo, TRUE);
  cinfo.out_color_space = JCS_RGB;
  jpeg_start_decompress(&cinfo);
  std::vector<uint8_t> rgb(static_cast<size_t>(cinfo.output_width) *
                           cinfo.output_height * 3);
  while (cinfo.output_scanline < cinfo.output_height) {
    JSAMPROW row =
        &rgb[static_cast<size_t>(cinfo.output_scanline) * cinfo.output_width * 3];
    jpeg_read_scanlines(&cinfo, &row, 1);
  }
  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  return rgb;
}

std::vector<uint8_t> Decode(DecodeContext& ctx,
                            const std::vector<uint8_t>& jpeg) {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgb;
  JpegDecodeOptions options;
  options.treat_ycbcr_as_rgb = false;
  EXPECT_TRUE(DecodeJpeg(ctx, jpeg, width, height, rgb, options));
  EXPECT_EQ(width, kWidth);
  EXPECT_EQ(height, kHeight);
  return rgb;
}

TEST(JpegDecodeModeTest, LibjpegSupported) {
  EXPECT_TRUE(JpegDecodeModeSupported(JpegDecodeMode::kLibjpeg));
}

TEST(JpegDecodeModeTest, LibjpegModeMatchesLibjpegDefaults) {
  const auto jpeg = EncodeTestJpeg();
  const auto reference = ReferenceDecode(jpeg);

  DecodeContext ctx;
  ctx.jpeg_decode_mode = JpegDecodeMode::kLibjpeg;
  EXPECT_EQ(Decode(ctx, jpeg), reference);
}

TEST(JpegDecodeModeTest, FastModeDiffersFromLibjpegDefaults) {
  const auto jpeg = EncodeTestJpeg();
  DecodeContext ctx;
  EXPECT_NE(Decode(ctx, jpeg), ReferenceDecode(jpeg));
}

// The decompressor is reused across decodes, so switching modes on one
// context must not leave settings from the previous decode behind.
TEST(JpegDecodeModeTest, SwitchingModesOnOneContext) {
  const auto jpeg = EncodeTestJpeg();
  DecodeContext fresh_fast;
  const auto fast = Decode(fresh_fast, jpeg);
  const auto reference = ReferenceDecode(jpeg);

  DecodeContext ctx;
  EXPECT_EQ(Decode(ctx, jpeg), fast);
  ctx.jpeg_decode_mode = JpegDecodeMode::kLibjpeg;
  EXPECT_EQ(Decode(ctx, jpeg), reference);
  ctx.jpeg_decode_mode = JpegDecodeMode::kFast;
  EXPECT_EQ(Decode(ctx, jpeg), fast);
}

#endif  // SIMPLETIFF_JPEG_BACKEND_JPGD

}  // namespace
}  // namespace simpletiff
