#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include "Vision.h"

namespace {

// =====================================================================
// Settings
// =====================================================================
constexpr int    PREVIEW_W = 320, PREVIEW_H = 240;  // size of the preview images sent to OpenGL
constexpr double MIN_AREA = 80.0;          // smallest blob (pixels) accepted as the marker
constexpr float  SMOOTH_ALPHA = 0.55f;     // weight of the newest measurement in the moving average
constexpr int    HOLD_FRAMES = 3;          // keep the last position this many frames after losing it
constexpr float  CALIB_HALF_FRAC = 0.07f;  // calibration box half-size, fraction of min(width, height)
constexpr int    HUE_TOL = 12;             // hue tolerance around the calibrated colour

struct HsvRange { int hLo, hHi, sLo, vLo; };   // OpenCV HSV: H 0..179, S and V 0..255

const HsvRange PRESETS[3] = {
    { 40,  85, 70, 50},    // Green
    {120, 150, 50, 40},    // Purple
    {150, 175, 60, 60},    // Pink
};
const char* PRESET_NAMES[3] = {"Green", "Purple", "Pink"};

// =====================================================================
// Shared state (protected by mtx)
// =====================================================================
cv::VideoCapture cap;
std::thread worker;
std::atomic<bool> running{false};
std::mutex mtx;

HsvRange range = PRESETS[0];
char colorName[32] = "Green (preset)";
char message[96] = "";
bool calibRequested = false;

VisionStatus status;
std::vector<unsigned char> previewRgb, previewMask;
unsigned previewId = 0;

// =====================================================================
// Image processing helpers
// =====================================================================
// Colour segmentation: pixels whose HSV is inside the range become 255, the rest 0
void thresholdHsv(const cv::Mat& hsv, const HsvRange& r, cv::Mat& mask) {
    if (r.hLo <= r.hHi) {
        cv::inRange(hsv, cv::Scalar(r.hLo, r.sLo, r.vLo), cv::Scalar(r.hHi, 255, 255), mask);
    } else {
        // Hue wraps around (for example 170..5): join two ranges
        cv::Mat a, b;
        cv::inRange(hsv, cv::Scalar(r.hLo, r.sLo, r.vLo), cv::Scalar(179, 255, 255), a);
        cv::inRange(hsv, cv::Scalar(0, r.sLo, r.vLo), cv::Scalar(r.hHi, 255, 255), b);
        cv::bitwise_or(a, b, mask);
    }
}

// Reads the colour inside the centre box and builds an HSV range around it.
// The object may fill only part of the box (a pen tip in front of a hand), so only the
// most saturated quarter of the pixels is used: a bright pen is far more colourful than skin.
bool calibrate(const cv::Mat& hsv, HsvRange& out, char* msg, size_t msgSize) {
    int half = (int)(std::min(hsv.cols, hsv.rows) * CALIB_HALF_FRAC);
    cv::Rect box(hsv.cols / 2 - half, hsv.rows / 2 - half, 2 * half, 2 * half);
    cv::Mat patch = hsv(box);

    // Saturation cut-off = 75th percentile of the box
    std::vector<int> allSats;
    for (int y = 0; y < patch.rows; y++)
        for (int x = 0; x < patch.cols; x++)
            allSats.push_back(patch.at<cv::Vec3b>(y, x)[1]);
    size_t q = allSats.size() * 3 / 4;
    std::nth_element(allSats.begin(), allSats.begin() + q, allSats.end());
    int satCut = allSats[q];

    // Hue is an angle (it wraps around), so take a circular mean of it.
    // Saturation and value: take the median.
    double sumSin = 0.0, sumCos = 0.0;
    std::vector<int> sats, vals;
    for (int y = 0; y < patch.rows; y++) {
        for (int x = 0; x < patch.cols; x++) {
            cv::Vec3b p = patch.at<cv::Vec3b>(y, x);
            if (p[1] < satCut) continue;               // skip the less colourful pixels
            double a = p[0] * 2.0 * CV_PI / 180.0;     // OpenCV hue 0..179 = 0..358 degrees
            sumSin += std::sin(a);
            sumCos += std::cos(a);
            sats.push_back(p[1]);
            vals.push_back(p[2]);
        }
    }
    std::nth_element(sats.begin(), sats.begin() + sats.size() / 2, sats.end());
    std::nth_element(vals.begin(), vals.begin() + vals.size() / 2, vals.end());
    int s = sats[sats.size() / 2];
    int v = vals[vals.size() / 2];

    if (s < 70) {
        std::snprintf(msg, msgSize, "Calibration failed: colour too pale (S=%d)", s);
        return false;
    }
    if (v < 40) {
        std::snprintf(msg, msgSize, "Calibration failed: too dark (V=%d)", v);
        return false;
    }

    double ang = std::atan2(sumSin, sumCos);
    if (ang < 0) ang += 2.0 * CV_PI;
    int h = (int)std::lround(ang * 180.0 / (2.0 * CV_PI)) % 180;

    out.hLo = (h - HUE_TOL + 180) % 180;
    out.hHi = (h + HUE_TOL) % 180;
    out.sLo = std::max(50, s - 70);
    out.vLo = std::max(40, v - 80);

    // Skin is orange-red (hue about 0-25). Warn if the sample looks like a hand or face.
    if (h <= 25 || h >= 170)
        std::snprintf(msg, msgSize, "H=%d S=%d V=%d  WARNING: looks like skin!", h, s, v);
    else
        std::snprintf(msg, msgSize, "Calibrated: H=%d  S=%d  V=%d", h, s, v);
    return true;
}

// Area and centroid of a contour polygon, written out by hand
// (gives the same result as cv::contourArea + cv::moments).
// Shoelace formula:  A  = 1/2 * sum( x_i*y_(i+1) - x_(i+1)*y_i )
// Polygon centroid:  Cx = 1/(6A) * sum( (x_i + x_(i+1)) * cross_i ),  same for Cy
bool polygonAreaCentroid(const std::vector<cv::Point>& c, double& area, double& cx, double& cy) {
    int n = (int)c.size();
    if (n < 3) return false;
    double a2 = 0.0, sx = 0.0, sy = 0.0;          // a2 = twice the signed area
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        double cross = (double)c[i].x * c[j].y - (double)c[j].x * c[i].y;
        a2 += cross;
        sx += (c[i].x + c[j].x) * cross;
        sy += (c[i].y + c[j].y) * cross;
    }
    if (std::fabs(a2) < 1e-9) return false;
    area = std::fabs(a2) / 2.0;
    cx = sx / (3.0 * a2);                         // 6A = 3 * a2 (signs cancel)
    cy = sy / (3.0 * a2);
    return true;
}

// =====================================================================
// Camera thread: capture -> blur -> HSV -> threshold -> morphology -> contour -> centroid
// =====================================================================
void loop() {
    using clock = std::chrono::steady_clock;

    cv::Mat frame, blurred, hsv, mask, small, smallMask;
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    std::vector<std::vector<cv::Point>> contours;

    bool haveSmooth = false;
    float sx = 0.5f, sy = 0.5f;               // smoothed position
    int missed = 1000;                        // frames since the marker was last seen

    int framesInSecond = 0, foundInSecond = 0;
    float fps = 0.0f, rate = 0.0f;
    auto secondStart = clock::now();

    while (running) {
        if (!cap.read(frame) || frame.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // 1. Mirror, so moving your hand right moves the blade right
        cv::flip(frame, frame, 1);
        // 2. Noise reduction
        cv::GaussianBlur(frame, blurred, cv::Size(5, 5), 0);
        // 3. BGR -> HSV (hue is separate from brightness, so lighting matters less)
        cv::cvtColor(blurred, hsv, cv::COLOR_BGR2HSV);

        HsvRange r;
        bool doCalib;
        {
            std::lock_guard<std::mutex> lock(mtx);
            r = range;
            doCalib = calibRequested;
            calibRequested = false;
        }
        if (doCalib) {
            HsvRange nr;
            char msg[96];
            bool ok = calibrate(hsv, nr, msg, sizeof(msg));
            std::lock_guard<std::mutex> lock(mtx);
            std::snprintf(message, sizeof(message), "%s", msg);
            if (ok) {
                range = r = nr;
                std::snprintf(colorName, sizeof(colorName), "Calibrated colour");
            }
        }

        // 4. Colour segmentation -> binary mask
        thresholdHsv(hsv, r, mask);
        // 5. Morphology: opening removes specks, dilation fills small holes
        cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
        cv::dilate(mask, mask, kernel);

        // 6. Largest contour above the minimum area = the marker
        contours.clear();
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        int best = -1;
        double bestArea = MIN_AREA, bestCx = 0.0, bestCy = 0.0;
        for (int i = 0; i < (int)contours.size(); i++) {
            double a, cx, cy;
            if (polygonAreaCentroid(contours[i], a, cx, cy) && a >= bestArea) {
                bestArea = a; best = i; bestCx = cx; bestCy = cy;
            }
        }

        bool foundNow = false;
        if (best >= 0) {
            // 7. Centroid of the marker (from the polygon moments above)
            float u = (float)bestCx / frame.cols;          // 0..1 left to right
            float v = 1.0f - (float)bestCy / frame.rows;   // 0..1 bottom to top
            // 8. Exponential moving average to reduce jitter
            if (!haveSmooth || missed > HOLD_FRAMES) {
                sx = u; sy = v;                            // re-acquired: no lag
            } else {
                sx = SMOOTH_ALPHA * u + (1.0f - SMOOTH_ALPHA) * sx;
                sy = SMOOTH_ALPHA * v + (1.0f - SMOOTH_ALPHA) * sy;
            }
            haveSmooth = true;
            missed = 0;
            foundNow = true;
        }
        if (!foundNow) missed++;

        // Statistics, refreshed once per second
        framesInSecond++;
        if (foundNow) foundInSecond++;
        float elapsed = std::chrono::duration<float>(clock::now() - secondStart).count();
        if (elapsed >= 1.0f) {
            fps  = framesInSecond / elapsed;
            rate = (float)foundInSecond / framesInSecond;
            framesInSecond = foundInSecond = 0;
            secondStart = clock::now();
        }

        // Small preview images for the OpenGL texture
        cv::resize(frame, small, cv::Size(PREVIEW_W, PREVIEW_H));
        cv::cvtColor(small, small, cv::COLOR_BGR2RGB);                 // OpenGL wants RGB
        cv::resize(mask, smallMask, cv::Size(PREVIEW_W, PREVIEW_H), 0, 0, cv::INTER_NEAREST);
        cv::cvtColor(smallMask, smallMask, cv::COLOR_GRAY2RGB);

        std::lock_guard<std::mutex> lock(mtx);
        status.running = true;
        status.found = haveSmooth && missed <= HOLD_FRAMES;
        status.x = sx;
        status.y = sy;
        status.cameraFps = fps;
        status.detectRate = rate;
        status.frameW = frame.cols;
        status.frameH = frame.rows;
        int half = (int)(std::min(frame.cols, frame.rows) * CALIB_HALF_FRAC);
        status.calibHalfU = (float)half / frame.cols;
        status.calibHalfV = (float)half / frame.rows;
        status.hLo = range.hLo;  status.hHi = range.hHi;
        status.sLo = range.sLo;  status.vLo = range.vLo;
        std::snprintf(status.colorName, sizeof(status.colorName), "%s", colorName);
        std::snprintf(status.message, sizeof(status.message), "%s", message);

        previewRgb.assign(small.data, small.data + small.total() * small.elemSize());
        previewMask.assign(smallMask.data, smallMask.data + smallMask.total() * smallMask.elemSize());
        previewId++;
    }
}

} // namespace

// =====================================================================
// Public functions
// =====================================================================
bool visionStart(int cameraIndex) {
    if (running) return true;

#ifdef _WIN32
    cap.open(cameraIndex, cv::CAP_DSHOW);    // DirectShow: skips the slow GStreamer attempt
#else
    cap.open(cameraIndex);
#endif
    if (!cap.isOpened()) return false;

    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);

    {
        std::lock_guard<std::mutex> lock(mtx);
        status = VisionStatus();
        status.running = true;
        std::snprintf(status.colorName, sizeof(status.colorName), "%s", colorName);
    }
    running = true;
    worker = std::thread(loop);
    return true;
}

void visionStop() {
    if (!running) return;
    running = false;
    if (worker.joinable()) worker.join();
    cap.release();
}

VisionStatus visionStatus() {
    std::lock_guard<std::mutex> lock(mtx);
    return status;
}

void visionSetPreset(int index) {
    if (index < 0 || index > 2) return;
    std::lock_guard<std::mutex> lock(mtx);
    range = PRESETS[index];
    std::snprintf(colorName, sizeof(colorName), "%s (preset)", PRESET_NAMES[index]);
    std::snprintf(message, sizeof(message), "Preset loaded: %s", PRESET_NAMES[index]);
}

void visionRequestCalibration() {
    std::lock_guard<std::mutex> lock(mtx);
    calibRequested = true;
}

bool visionGetPreview(unsigned lastId, unsigned& newId,
                      std::vector<unsigned char>& rgb, std::vector<unsigned char>& mask,
                      int& w, int& h) {
    std::lock_guard<std::mutex> lock(mtx);
    if (previewId == 0 || previewId == lastId) return false;
    rgb = previewRgb;
    mask = previewMask;
    w = PREVIEW_W;
    h = PREVIEW_H;
    newId = previewId;
    return true;
}
