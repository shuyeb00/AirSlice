#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <chrono>

int main() {
    std::cout << "OpenCV version: " << CV_VERSION << "\n";

    // Open the default webcam (index 0)
    cv::VideoCapture cap(0);
    if (!cap.isOpened()) {
        std::cerr << "ERROR: could not open webcam 0\n";
        return 1;
    }
    std::cout << "Backend: " << cap.getBackendName() << "\n";

    // Ask for 640x480, the size the game will use
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);

    // Read 60 frames and time it
    cv::Mat frame;
    int got = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 60; i++) {
        if (cap.read(frame) && !frame.empty()) got++;
    }
    auto t1 = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();

    if (frame.empty()) {
        std::cerr << "ERROR: camera opened but gave no frames\n";
        return 1;
    }

    std::cout << "Frames read: " << got << "/60\n";
    std::cout << "Frame size: " << frame.cols << " x " << frame.rows << "\n";
    std::cout << "Approx FPS: " << got / sec << "\n";

    // Test one image processing call: BGR -> HSV
    cv::Mat hsv;
    cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);
    cv::Scalar m = cv::mean(hsv);
    std::cout << "Mean H S V: " << m[0] << " " << m[1] << " " << m[2] << "\n";

    // Save the last frame so you can look at it
    cv::imwrite("test_frame.jpg", frame);
    std::cout << "Saved test_frame.jpg\n";
    return 0;
}