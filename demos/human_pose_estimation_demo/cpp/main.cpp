/*
// Copyright (C) 2018-2022 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gflags/gflags.h>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <openvino/openvino.hpp>

#include <models/hpe_model_associative_embedding.h>
#include <models/hpe_model_openpose.h>
#include <models/input_data.h>
#include <models/model_base.h>
#include <models/results.h>
#include <monitors/presenter.h>
#include <pipelines/async_pipeline.h>
#include <pipelines/metadata.h>
#include <utils/common.hpp>
#include <utils/config_factory.h>
#include <utils/default_flags.hpp>
#include <utils/image_utils.h>
#include <utils/images_capture.h>
#include <utils/ocv_common.hpp>
#include <utils/performance_metrics.hpp>
#include <utils/slog.hpp>

#include <atomic>
#include <stdio.h>
#include <queue>
#include <thread>
#include <condition_variable>

#include <opencv2/dnn/dnn.hpp>
#include <opencv2/imgcodecs.hpp>

//#include <libnlab-ctrl.hpp>
#include <VimbaCPP/Include/VimbaCPP.h>

#include <ncam/MJPEGStreamer.hpp>
#include <ncam/BufferedChannel.hpp>
#include <ncam/Camera.hpp>

using cv::Mat;
using std::vector;

#define INFERENCE_CHANNEL_SIZE 3
#define JPEG_ENCODING_CHANNEL_SIZE 3
#define MAX_FRAME_BUFFERS 5

// The local port for the MJPEG server.
#define MJPEG_PORT 8080

ncam::BufferedChannel<Mat>           jpegEncodeChan(JPEG_ENCODING_CHANNEL_SIZE);
//ncam::BufferedChannel<vector<uchar>> videoChan(VIDEO_CHANNEL_SIZE);
ncam::BufferedChannel<Mat>           infChan(INFERENCE_CHANNEL_SIZE);
//ncam::BufferedChannel<vector<Rect>>  infResChan(INFERENCE_RESULT_CHANNEL_SIZE);

DEFINE_INPUT_FLAGS
DEFINE_OUTPUT_FLAGS

static const char help_message[] = "Print a usage message.";
static const char at_message[] = "Required. Type of the model, either 'ae' for Associative Embedding, 'higherhrnet' "
                                 "for HigherHRNet models based on ae "
                                 "or 'openpose' for OpenPose.";
static const char model_message[] = "Required. Path to an .xml file with a trained model.";
static const char layout_message[] = "Optional. Specify inputs layouts."
                                     " Ex. NCHW or input0:NCHW,input1:NC in case of more than one input.";
static const char target_size_message[] = "Optional. Target input size.";
static const char target_device_message[] =
    "Optional. Specify the target device to infer on (the list of available devices is shown below). "
    "Default value is CPU. Use \"-d HETERO:<comma-separated_devices_list>\" format to specify HETERO plugin. "
    "The demo will look for a suitable plugin for a specified device.";
static const char thresh_output_message[] = "Optional. Probability threshold for poses filtering.";
static const char nireq_message[] = "Optional. Number of infer requests. If this option is omitted, number of infer "
                                    "requests is determined automatically.";
static const char num_threads_message[] = "Optional. Number of threads.";
static const char num_streams_message[] = "Optional. Number of streams to use for inference on the CPU or/and GPU in "
                                          "throughput mode (for HETERO and MULTI device cases use format "
                                          "<device1>:<nstreams1>,<device2>:<nstreams2> or just <nstreams>)";
static const char no_show_message[] = "Optional. Don't show output.";
static const char utilization_monitors_message[] = "Optional. List of monitors to show initially.";
static const char output_resolution_message[] =
    "Optional. Specify the maximum output window resolution "
    "in (width x height) format. Example: 1280x720. Input frame size used by default.";

DEFINE_bool(h, false, help_message);
DEFINE_string(at, "", at_message);
DEFINE_string(m, "", model_message);
DEFINE_string(layout, "", layout_message);
DEFINE_uint32(tsize, 0, target_size_message);
DEFINE_string(d, "CPU", target_device_message);
DEFINE_double(t, 0.1, thresh_output_message);
DEFINE_uint32(nireq, 0, nireq_message);
DEFINE_uint32(nthreads, 0, num_threads_message);
DEFINE_string(nstreams, "", num_streams_message);
DEFINE_bool(no_show, false, no_show_message);
DEFINE_string(u, "", utilization_monitors_message);
DEFINE_string(output_resolution, "", output_resolution_message);

/**
 * \brief This function shows a help message
 */
static void showUsage() {
    std::cout << std::endl;
    std::cout << "human_pose_estimation_demo [OPTION]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << std::endl;
    std::cout << "    -h                        " << help_message << std::endl;
    std::cout << "    -at \"<type>\"              " << at_message << std::endl;
    std::cout << "    -i                        " << input_message << std::endl;
    std::cout << "    -m \"<path>\"               " << model_message << std::endl;
    std::cout << "    -layout \"<string>\"        " << layout_message << std::endl;
    std::cout << "    -o \"<path>\"               " << output_message << std::endl;
    std::cout << "    -limit \"<num>\"            " << limit_message << std::endl;
    std::cout << "    -tsize                    " << target_size_message << std::endl;
    std::cout << "    -d \"<device>\"             " << target_device_message << std::endl;
    std::cout << "    -t                        " << thresh_output_message << std::endl;
    std::cout << "    -nireq \"<integer>\"        " << nireq_message << std::endl;
    std::cout << "    -nthreads \"<integer>\"     " << num_threads_message << std::endl;
    std::cout << "    -nstreams                 " << num_streams_message << std::endl;
    std::cout << "    -loop                     " << loop_message << std::endl;
    std::cout << "    -no_show                  " << no_show_message << std::endl;
    std::cout << "    -output_resolution        " << output_resolution_message << std::endl;
    std::cout << "    -u                        " << utilization_monitors_message << std::endl;
}

bool ParseAndCheckCommandLine(int argc, char* argv[]) {
    // ---------------------------Parsing and validation of input args--------------------------------------
    gflags::ParseCommandLineNonHelpFlags(&argc, &argv, true);
    if (FLAGS_h) {
        showUsage();
        showAvailableDevices();
        return false;
    }

    if (FLAGS_i.empty()) {
        throw std::logic_error("Parameter -i is not set");
    }

    if (FLAGS_m.empty()) {
        throw std::logic_error("Parameter -m is not set");
    }

    if (FLAGS_at.empty()) {
        throw std::logic_error("Parameter -at is not set");
    }

    if (!FLAGS_output_resolution.empty() && FLAGS_output_resolution.find("x") == std::string::npos) {
        throw std::logic_error("Correct format of -output_resolution parameter is \"width\"x\"height\".");
    }
    return true;
}

cv::Mat renderHumanPose(HumanPoseResult& result, OutputTransform& outputTransform) {
    if (!result.metaData) {
        throw std::invalid_argument("Renderer: metadata is null");
    }

    auto outputImg = result.metaData->asRef<ImageMetaData>().img;

    if (outputImg.empty()) {
        throw std::invalid_argument("Renderer: image provided in metadata is empty");
    }
    outputTransform.resize(outputImg);
    static const cv::Scalar colors[HPEOpenPose::keypointsNumber] = {cv::Scalar(255, 0, 0),
                                                                    cv::Scalar(255, 85, 0),
                                                                    cv::Scalar(255, 170, 0),
                                                                    cv::Scalar(255, 255, 0),
                                                                    cv::Scalar(170, 255, 0),
                                                                    cv::Scalar(85, 255, 0),
                                                                    cv::Scalar(0, 255, 0),
                                                                    cv::Scalar(0, 255, 85),
                                                                    cv::Scalar(0, 255, 170),
                                                                    cv::Scalar(0, 255, 255),
                                                                    cv::Scalar(0, 170, 255),
                                                                    cv::Scalar(0, 85, 255),
                                                                    cv::Scalar(0, 0, 255),
                                                                    cv::Scalar(85, 0, 255),
                                                                    cv::Scalar(170, 0, 255),
                                                                    cv::Scalar(255, 0, 255),
                                                                    cv::Scalar(255, 0, 170),
                                                                    cv::Scalar(255, 0, 85)};
    static const std::pair<int, int> keypointsOP[] = {{1, 2},
                                                      {1, 5},
                                                      {2, 3},
                                                      {3, 4},
                                                      {5, 6},
                                                      {6, 7},
                                                      {1, 8},
                                                      {8, 9},
                                                      {9, 10},
                                                      {1, 11},
                                                      {11, 12},
                                                      {12, 13},
                                                      {1, 0},
                                                      {0, 14},
                                                      {14, 16},
                                                      {0, 15},
                                                      {15, 17}};
    static const std::pair<int, int> keypointsAE[] = {{15, 13},
                                                      {13, 11},
                                                      {16, 14},
                                                      {14, 12},
                                                      {11, 12},
                                                      {5, 11},
                                                      {6, 12},
                                                      {5, 6},
                                                      {5, 7},
                                                      {6, 8},
                                                      {7, 9},
                                                      {8, 10},
                                                      {1, 2},
                                                      {0, 1},
                                                      {0, 2},
                                                      {1, 3},
                                                      {2, 4},
                                                      {3, 5},
                                                      {4, 6}};
    const int stickWidth = 4;
    const cv::Point2f absentKeypoint(-1.0f, -1.0f);
    for (auto& pose : result.poses) {
        for (size_t keypointIdx = 0; keypointIdx < pose.keypoints.size(); keypointIdx++) {
            if (pose.keypoints[keypointIdx] != absentKeypoint) {
                outputTransform.scaleCoord(pose.keypoints[keypointIdx]);
                cv::circle(outputImg, pose.keypoints[keypointIdx], 4, colors[keypointIdx], -1);
            }
        }
    }
    std::vector<std::pair<int, int>> limbKeypointsIds;
    if (!result.poses.empty()) {
        if (result.poses[0].keypoints.size() == HPEOpenPose::keypointsNumber) {
            limbKeypointsIds.insert(limbKeypointsIds.begin(), std::begin(keypointsOP), std::end(keypointsOP));
        } else {
            limbKeypointsIds.insert(limbKeypointsIds.begin(), std::begin(keypointsAE), std::end(keypointsAE));
        }
    }
    cv::Mat pane = outputImg.clone();
    for (auto pose : result.poses) {
        for (const auto& limbKeypointsId : limbKeypointsIds) {
            std::pair<cv::Point2f, cv::Point2f> limbKeypoints(pose.keypoints[limbKeypointsId.first],
                                                              pose.keypoints[limbKeypointsId.second]);
            if (limbKeypoints.first == absentKeypoint || limbKeypoints.second == absentKeypoint) {
                continue;
            }

            float meanX = (limbKeypoints.first.x + limbKeypoints.second.x) / 2;
            float meanY = (limbKeypoints.first.y + limbKeypoints.second.y) / 2;
            cv::Point difference = limbKeypoints.first - limbKeypoints.second;
            double length = std::sqrt(difference.x * difference.x + difference.y * difference.y);
            int angle = static_cast<int>(std::atan2(difference.y, difference.x) * 180 / CV_PI);
            std::vector<cv::Point> polygon;
            cv::ellipse2Poly(cv::Point2d(meanX, meanY), cv::Size2d(length / 2, stickWidth), angle, 0, 360, 1, polygon);
            cv::fillConvexPoly(pane, polygon, colors[limbKeypointsId.second]);
        }
    }
    cv::addWeighted(outputImg, 0.4, pane, 0.6, 0, outputImg);
    return outputImg;
}


int main(int argc, char* argv[]) {
    try {
        PerformanceMetrics metrics, renderMetrics;

        // ------------------------------ Parsing and validation of input args ---------------------------------
        if (!ParseAndCheckCommandLine(argc, argv)) {
            return 0;
        }

        //------------------------------- Preparing Input ------------------------------------------------------

        // Create camera.
        ncam::Camera cam = ncam::Camera();
        cam.printSystemVersion();

        // Start the camera.
        bool ok = cam.start(MAX_FRAME_BUFFERS);
        if (!ok) {
            return 1;
        }

        auto startTime = std::chrono::steady_clock::now();
        cv::Mat curr_frame;
        if (!cam.read(curr_frame)) {
            return 1;
        }

        OutputTransform outputTransform = OutputTransform();
        cv::Size outputResolution = curr_frame.size();
        size_t found = FLAGS_output_resolution.find("x");
        if (found != std::string::npos) {
            outputResolution =
                cv::Size{std::stoi(FLAGS_output_resolution.substr(0, found)),
                         std::stoi(FLAGS_output_resolution.substr(found + 1, FLAGS_output_resolution.length()))};
            outputTransform = OutputTransform(curr_frame.size(), outputResolution);
            outputResolution = outputTransform.computeResolution();
        }

        // Our motion jpeg server.
        ncam::MJPEGStreamer streamer;
        streamer.start(MJPEG_PORT, 1);
        std::cout << "MJPEG server listening on port " << std::to_string(MJPEG_PORT) << std::endl;

        //------------------------------ Running Human Pose Estimation routines
        //----------------------------------------------

        double aspectRatio = curr_frame.cols / static_cast<double>(curr_frame.rows);
        std::unique_ptr<ModelBase> model;
        if (FLAGS_at == "openpose") {
            model.reset(new HPEOpenPose(FLAGS_m, aspectRatio, FLAGS_tsize, static_cast<float>(FLAGS_t), FLAGS_layout));
        } else if (FLAGS_at == "ae") {
            model.reset(new HpeAssociativeEmbedding(FLAGS_m,
                                                    aspectRatio,
                                                    FLAGS_tsize,
                                                    static_cast<float>(FLAGS_t),
                                                    FLAGS_layout));
        } else if (FLAGS_at == "higherhrnet") {
            float delta = 0.5f;
            model.reset(new HpeAssociativeEmbedding(FLAGS_m,
                                                    aspectRatio,
                                                    FLAGS_tsize,
                                                    static_cast<float>(FLAGS_t),
                                                    FLAGS_layout,
                                                    delta,
                                                    RESIZE_KEEP_ASPECT_LETTERBOX));
        } else {
            slog::err << "No model type or invalid model type (-at) provided: " + FLAGS_at << slog::endl;
            return -1;
        }

        slog::info << ov::get_openvino_version() << slog::endl;
        ov::Core core;

        AsyncPipeline pipeline(std::move(model),
                               ConfigFactory::getUserConfig(FLAGS_d, FLAGS_nireq, FLAGS_nstreams, FLAGS_nthreads),
                               core);
        Presenter presenter(FLAGS_u);

        int64_t frameNum =
            pipeline.submitData(ImageInputData(curr_frame), std::make_shared<ImageMetaData>(curr_frame, startTime));

        uint32_t framesProcessed = 0;
        bool keepRunning = true;
        std::unique_ptr<ResultBase> result;

        while (keepRunning) {
            if (pipeline.isReadyToProcess()) {
                //--- Capturing frame
                startTime = std::chrono::steady_clock::now();
                if (!cam.read(curr_frame) || curr_frame.empty()) {
                    std::cout << "no frame received" << std::endl;
                    // Input stream is over
                    break;
                }
                frameNum = pipeline.submitData(ImageInputData(curr_frame),
                                               std::make_shared<ImageMetaData>(curr_frame, startTime));
            }

            //--- Waiting for free input slot or output data available. Function will return immediately if any of them
            // are available.
            pipeline.waitForData();

            //--- Checking for results and rendering data if it's ready
            //--- If you need just plain data without rendering - cast result's underlying pointer to HumanPoseResult*
            //    and use your own processing instead of calling renderHumanPose().
            while (keepRunning && (result = pipeline.getResult())) {
                auto renderingStart = std::chrono::steady_clock::now();
                cv::Mat outFrame = renderHumanPose(result->asRef<HumanPoseResult>(), outputTransform);
                //--- Showing results and device information
                presenter.drawGraphs(outFrame);
                renderMetrics.update(renderingStart);
                metrics.update(result->metaData->asRef<ImageMetaData>().timeStamp,
                               outFrame,
                               {10, 22},
                               cv::FONT_HERSHEY_COMPLEX,
                               0.65);
                streamer.publish("/stream", outFrame);
                framesProcessed++;
                if (!FLAGS_no_show) {
                    cv::imshow("Human Pose Estimation Results", outFrame);
                    //--- Processing keyboard events
                    int key = cv::waitKey(1);
                    if (27 == key || 'q' == key || 'Q' == key) {  // Esc
                        keepRunning = false;
                    } else {
                        presenter.handleKey(key);
                    }
                }
            }
        }

        // ------------ Waiting for completion of data processing and rendering the rest of results ---------
        pipeline.waitForTotalCompletion();
        for (; framesProcessed <= frameNum; framesProcessed++) {
            while (!(result = pipeline.getResult())) {}
            auto renderingStart = std::chrono::steady_clock::now();
            cv::Mat outFrame = renderHumanPose(result->asRef<HumanPoseResult>(), outputTransform);
            //--- Showing results and device information
            presenter.drawGraphs(outFrame);
            renderMetrics.update(renderingStart);
            metrics.update(result->metaData->asRef<ImageMetaData>().timeStamp,
                           outFrame,
                           {10, 22},
                           cv::FONT_HERSHEY_COMPLEX,
                           0.65);
            streamer.publish("/stream", outFrame);
            if (!FLAGS_no_show) {
                cv::imshow("Human Pose Estimation Results", outFrame);
                //--- Updating output window
                cv::waitKey(1);
            }
        }

        slog::info << "Metrics report:" << slog::endl;
        metrics.logTotal();
        //logLatencyPerStage(cam.getTotal().latency,
        //                   pipeline.getPreprocessMetrics().getTotal().latency,
        //                   pipeline.getInferenceMetircs().getTotal().latency,
        //                   pipeline.getPostprocessMetrics().getTotal().latency,
        //                   renderMetrics.getTotal().latency);

        // Stop video streamer.
        streamer.stop();

        slog::info << presenter.reportMeans() << slog::endl;
    } catch (const std::exception& error) {
        slog::err << error.what() << slog::endl;
        return 1;
    } catch (...) {
        slog::err << "Unknown/internal exception happened." << slog::endl;
        return 1;
    }

    return 0;
}
