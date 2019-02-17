// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_TEST_VIDEO_FRAME_FILE_WRITER_H_
#define MEDIA_GPU_TEST_VIDEO_FRAME_FILE_WRITER_H_

#include <memory>

#include "base/files/file_path.h"
#include "base/memory/scoped_refptr.h"
#include "base/sequence_checker.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread.h"
#include "media/gpu/test/video_frame_helpers.h"

namespace media {
namespace test {

// Default output folder used to store frames.
constexpr const base::FilePath::CharType* kDefaultOutputFolder =
    FILE_PATH_LITERAL("video_frames");

class VideoFrameMapper;

// The video frame file writer class implements functionality to write video
// frames to file. The supported output formats are PNG and raw I420 YUV.
class VideoFrameFileWriter : public VideoFrameProcessor {
 public:
  // Supported output formats.
  enum class OutputFormat {
    kPNG = 0,
    kYUV,
  };

  ~VideoFrameFileWriter() override;

  // Create an instance of the video frame file writer.
  static std::unique_ptr<VideoFrameFileWriter> Create(
      const base::FilePath& output_folder =
          base::FilePath(kDefaultOutputFolder),
      OutputFormat output_format = OutputFormat::kPNG);

  // Interface VideoFrameProcessor
  void ProcessVideoFrame(scoped_refptr<const VideoFrame> video_frame,
                         size_t frame_index) override;
  // Wait until all currently scheduled frame write operations are done.
  bool WaitUntilDone() override;

 private:
  VideoFrameFileWriter(const base::FilePath& output_folder,
                       OutputFormat output_format);

  // Initialize the video frame file writer.
  bool Initialize();

  // Writes the specified video frame to file on the |file_writer_thread_|.
  void ProcessVideoFrameTask(scoped_refptr<const VideoFrame> video_frame,
                             size_t frame_index);

  // Write the video frame to disk in PNG format.
  void WriteVideoFramePNG(scoped_refptr<const VideoFrame> video_frame,
                          const base::FilePath& filename);
  // Write the video frame to disk in I420 YUV format.
  void WriteVideoFrameYUV(scoped_refptr<const VideoFrame> video_frame,
                          const base::FilePath& filename);

  // Output folder the frames will be written to.
  const base::FilePath output_folder_;
  // Output format of the frames.
  const OutputFormat output_format_;

  // The video frame mapper used to gain access to the raw video frame memory.
  std::unique_ptr<VideoFrameMapper> video_frame_mapper_;

  // The number of frames currently queued for writing.
  size_t num_frames_writing_ GUARDED_BY(frame_writer_lock_);

  // Thread on which video frame writing is done.
  base::Thread frame_writer_thread_;
  mutable base::Lock frame_writer_lock_;
  mutable base::ConditionVariable frame_writer_cv_;

  SEQUENCE_CHECKER(writer_sequence_checker_);
  SEQUENCE_CHECKER(writer_thread_sequence_checker_);

  DISALLOW_COPY_AND_ASSIGN(VideoFrameFileWriter);
};

}  // namespace test
}  // namespace media

#endif  // MEDIA_GPU_TEST_VIDEO_FRAME_FILE_WRITER_H_