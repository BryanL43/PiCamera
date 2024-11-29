#ifndef _FRAME_PROCESSOR_HPP_
#define _FRAME_PROCESSOR_HPP_

#include <iostream>
#include <opencv2/opencv.hpp>
#include <vector>

class FrameProcessor {
public:
    FrameProcessor(int numOfSlices, double meanIntensityMult,
                    int minThreshold, int maxThreshold, bool debug);
    ~FrameProcessor();

    void processFrame(cv::Mat &frame, unsigned int height, unsigned int width,
                        const uint8_t* buffer);

private:
    int slices;
    double meanIntensityMult;
    int minThreshold;
    int maxThreshold;
    bool debugMode = false;

    cv::Point processSlice(cv::Mat &slice, int sliceIndex, cv::Mat &frame, int sliceHeight);
};

#endif