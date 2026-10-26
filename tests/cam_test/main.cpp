#include <GL/glut.h>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <cstdio>
#include <cstdlib>

cv::VideoCapture cap;
cv::Mat frame, rgb;
GLuint tex = 0;          // OpenGL texture that holds the camera image
bool texReady = false;

int frameCount = 0;      // for measuring camera FPS
int lastTime = 0;
float fps = 0.0f;

// Read one camera frame and copy it into the texture
void updateTexture() {
    if (!cap.read(frame) || frame.empty()) return;
    cv::flip(frame, frame, 1);                    // mirror, like a selfie camera
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);  // OpenCV uses BGR, OpenGL wants RGB

    glBindTexture(GL_TEXTURE_2D, tex);
    if (!texReady) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, rgb.cols, rgb.rows, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, rgb.data);
        texReady = true;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rgb.cols, rgb.rows,
                        GL_RGB, GL_UNSIGNED_BYTE, rgb.data);
    }
    frameCount++;
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();

    if (texReady) {
        // Draw a rectangle covering the window, painted with the camera texture.
        // Texture y is flipped because OpenCV rows go top-down, OpenGL goes bottom-up.
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex);
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f,   0.0f);
            glTexCoord2f(1.0f, 1.0f); glVertex2f(640.0f, 0.0f);
            glTexCoord2f(1.0f, 0.0f); glVertex2f(640.0f, 480.0f);
            glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f,   480.0f);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }

    // FPS text in yellow so it shows on top of the video
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Camera FPS: %.1f   (ESC to quit)", fps);
    glColor3f(1.0f, 1.0f, 0.0f);
    glRasterPos2f(10.0f, 455.0f);
    for (const char* c = buf; *c; c++)
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *c);

    glutSwapBuffers();
}

void timer(int) {
    updateTexture();

    int now = glutGet(GLUT_ELAPSED_TIME);   // milliseconds since start
    if (now - lastTime >= 1000) {
        fps = frameCount * 1000.0f / (now - lastTime);
        frameCount = 0;
        lastTime = now;
    }
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

void reshape(int w, int h) {
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0.0, 640.0, 0.0, 480.0);
    glMatrixMode(GL_MODELVIEW);
}

void keyboard(unsigned char key, int, int) {
    if (key == 27) std::exit(0);
}

int main(int argc, char** argv) {
    // CAP_DSHOW = use DirectShow directly (skips the slow GStreamer attempt)
    cap.open(0, cv::CAP_DSHOW);
    if (!cap.isOpened()) {
        std::printf("ERROR: could not open webcam\n");
        return 1;
    }
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(640, 480);
    glutCreateWindow("AirSlice - camera texture test");

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutTimerFunc(16, timer, 0);
    glutMainLoop();
    return 0;
}
