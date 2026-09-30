#include "vision/detector.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
constexpr int INPUT_ELEMENTS = 1 * 3 * 640 * 640;
constexpr int OUTPUT_ELEMENTS = 1 * 84 * 8400;
}

void Detector::Logger::log(Severity severity, const char* message) noexcept
{
  if (severity <= Severity::kWARNING)
  {
    std::cerr << "[TensorRT] " << message << '\n';
  }
}

Detector::Detector(const std::string& model_path)
  : runtime_(nullptr),
    engine_(nullptr),
    context_(nullptr),
    device_input_(nullptr),
    device_output_(nullptr),
    stream_(nullptr),
    output_buffer_(OUTPUT_ELEMENTS)
{
  std::ifstream engine_file(model_path, std::ios::binary);

  if (!engine_file)
  {
    throw std::runtime_error(
      "Failed to open TensorRT engine: " + model_path);
  }

  engine_file.seekg(0, std::ios::end);
  const std::streamsize engine_size = engine_file.tellg();
  engine_file.seekg(0, std::ios::beg);

  if (engine_size <= 0)
  {
    throw std::runtime_error(
      "Invalid TensorRT engine: " + model_path);
  }

  std::vector<char> engine_data(static_cast<std::size_t>(engine_size));

  if (!engine_file.read(engine_data.data(), engine_size))
  {
    throw std::runtime_error(
      "Failed to read TensorRT engine: " + model_path);
  }

  runtime_ = nvinfer1::createInferRuntime(logger_);

  if (runtime_ == nullptr)
  {
    throw std::runtime_error("Failed to create TensorRT runtime");
  }

  engine_ = runtime_->deserializeCudaEngine(
    engine_data.data(),
    static_cast<std::size_t>(engine_size),
    nullptr);

  if (engine_ == nullptr)
  {
    throw std::runtime_error("Failed to deserialize TensorRT engine");
  }

  context_ = engine_->createExecutionContext();

  if (context_ == nullptr)
  {
    throw std::runtime_error("Failed to create TensorRT execution context");
  }

  if (engine_->getNbBindings() != 2)
  {
    throw std::runtime_error("Unexpected TensorRT binding count");
  }

  const int input_index = engine_->getBindingIndex("images");
  const int output_index = engine_->getBindingIndex("output0");

  if (input_index < 0 || output_index < 0)
  {
    throw std::runtime_error("Failed to find TensorRT bindings");
  }

  if (cudaMalloc(
        &device_input_,
        INPUT_ELEMENTS * sizeof(float)) != cudaSuccess)
  {
    throw std::runtime_error("Failed to allocate CUDA input buffer");
  }

  if (cudaMalloc(
        &device_output_,
        OUTPUT_ELEMENTS * sizeof(float)) != cudaSuccess)
  {
    throw std::runtime_error("Failed to allocate CUDA output buffer");
  }

  if (cudaStreamCreate(&stream_) != cudaSuccess)
  {
    throw std::runtime_error("Failed to create CUDA stream");
  }
}

Detector::~Detector()
{
  if (stream_ != nullptr)
  {
    cudaStreamDestroy(stream_);
  }

  if (device_output_ != nullptr)
  {
    cudaFree(device_output_);
  }

  if (device_input_ != nullptr)
  {
    cudaFree(device_input_);
  }

  if (context_ != nullptr)
  {
    context_->destroy();
  }

  if (engine_ != nullptr)
  {
    engine_->destroy();
  }

  if (runtime_ != nullptr)
  {
    runtime_->destroy();
  }
}

std::vector<cv::Mat> Detector::infer(const cv::Mat& input_blob)
{
  if (!isLoaded())
  {
    throw std::runtime_error("TensorRT detector is not loaded");
  }

  if (input_blob.empty() ||
      input_blob.type() != CV_32F ||
      input_blob.total() != INPUT_ELEMENTS)
  {
    throw std::runtime_error("Unexpected TensorRT input blob");
  }

  const int input_index = engine_->getBindingIndex("images");
  const int output_index = engine_->getBindingIndex("output0");

  void* bindings[2] = {};
  bindings[input_index] = device_input_;
  bindings[output_index] = device_output_;

  const std::size_t input_bytes = INPUT_ELEMENTS * sizeof(float);
  const std::size_t output_bytes = OUTPUT_ELEMENTS * sizeof(float);

  if (cudaMemcpyAsync(
        device_input_,
        input_blob.ptr<float>(),
        input_bytes,
        cudaMemcpyHostToDevice,
        stream_) != cudaSuccess)
  {
    throw std::runtime_error("CUDA input copy failed");
  }

  if (!context_->enqueueV2(bindings, stream_, nullptr))
  {
    throw std::runtime_error("TensorRT inference failed");
  }

  if (cudaMemcpyAsync(
        output_buffer_.data(),
        device_output_,
        output_bytes,
        cudaMemcpyDeviceToHost,
        stream_) != cudaSuccess)
  {
    throw std::runtime_error("CUDA output copy failed");
  }

  if (cudaStreamSynchronize(stream_) != cudaSuccess)
  {
    throw std::runtime_error("CUDA stream synchronization failed");
  }

  int dimensions[] = {1, 84, 8400};

  cv::Mat output(
    3,
    dimensions,
    CV_32F,
    output_buffer_.data());

  return {output.clone()};
}

bool Detector::isLoaded() const
{
  return runtime_ != nullptr &&
         engine_ != nullptr &&
         context_ != nullptr &&
         device_input_ != nullptr &&
         device_output_ != nullptr &&
         stream_ != nullptr;
}