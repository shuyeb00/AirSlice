#include <GL/glut.h>
#include <cstdio>
#include <cstdlib>

float angle = 0.0f;   // current rotation of the triangle, in degrees

// Draws one frame
void display() {
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();

    // Move to the centre of the window, rotate, then draw the triangle
    glPushMatrix();
    glTranslatef(400.0f, 300.0f, 0.0f);
    glRotatef(angle, 0.0f, 0.0f, 1.0f);
    glBegin(GL_TRIANGLES);
        glColor3f(1.0f, 0.3f, 0.2f); glVertex2f(0.0f, 120.0f);
        glColor3f(0.2f, 1.0f, 0.3f); glVertex2f(-104.0f, -60.0f);
        glColor3f(0.2f, 0.4f, 1.0f); glVertex2f(104.0f, -60.0f);
    glEnd();
    glPopMatrix();

    // Text in the top-left corner
    glColor3f(1.0f, 1.0f, 1.0f);
    glRasterPos2f(20.0f, 570.0f);
    const char* msg = "AirSlice GLUT test - press ESC to quit";
    for (const char* c = msg; *c; c++)
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *c);

    glutSwapBuffers();   // show the finished frame (double buffering)
}

// Runs about 60 times per second: update the angle, ask for a redraw
void timer(int) {
    angle += 2.0f;
    if (angle >= 360.0f) angle -= 360.0f;
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);   // 16 ms later, run again
}

// Called when the window opens or is resized
void reshape(int w, int h) {
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0.0, 800.0, 0.0, 600.0);   // 2D world: x 0..800, y 0..600
    glMatrixMode(GL_MODELVIEW);
}

void keyboard(unsigned char key, int, int) {
    if (key == 27) std::exit(0);   // 27 = ESC
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(800, 600);
    glutCreateWindow("AirSlice - GLUT test");

    // Print which OpenGL your laptop provides
    std::printf("OpenGL version: %s\n", (const char*)glGetString(GL_VERSION));
    std::printf("Renderer: %s\n", (const char*)glGetString(GL_RENDERER));

    glClearColor(0.08f, 0.08f, 0.12f, 1.0f);   // dark background
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutTimerFunc(16, timer, 0);
    glutMainLoop();
    return 0;
}
