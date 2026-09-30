#ifndef DETECTOR_HPP
#define DETECTOR_HPP

#include <NvInfer.h>
#include <cuda_runtime_api.h>
#include <opencv2/core.hpp>

#include <string>
#include <vector>

class Detector
{
public:
  explicit Detector(const std::string& model_path);
  ~Detector();

  Detector(const Detector&) = delete;
  Detector& operator=(const Detector&) = delete;

  std::vector<cv::Mat> infer(const cv::Mat& input_blob);
  bool isLoaded() const;

private:
  class Logger : public nvinfer1::ILogger
  {
  public:
    void log(Severity severity, const char* message) noexcept override;
  };

  Logger logger_;

  nvinfer1::IRuntime* runtime_;
  nvinfer1::ICudaEngine* engine_;
  nvinfer1::IExecutionContext* context_;

  void* device_input_;
  void* device_output_;

  cudaStream_t stream_;

  std::vector<float> output_buffer_;
};

#endif // DETECTOR_HPP