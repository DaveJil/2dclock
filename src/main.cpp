// macOS + GLFW + Apple OpenGL (no GLAD/GLEW)
// Build: mkdir build && cd build && cmake .. && cmake --build .
// Run:   ./clock2d

#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/gl3.h>
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <vector>
#include <chrono>
#include <ctime>

// GLSL 1.50 core (OpenGL 3.2 Core)
static const char* kVS = R"GLSL(
#version 150 core
layout (location = 0) in vec2 aPos;

uniform float uAngle;      // radians (clockwise positive by our convention)
uniform vec2  uScale;      // scale in NDC space
uniform vec2  uTranslate;  // translate in NDC space

void main() {
    float c = cos(uAngle), s = sin(uAngle);
    vec2 p = vec2(c*aPos.x - s*aPos.y, s*aPos.x + c*aPos.y);
    p = p * uScale + uTranslate;
    gl_Position = vec4(p, 0.0, 1.0);
}
)GLSL";

static const char* kFS = R"GLSL(
#version 150 core
out vec4 FragColor;
uniform vec3 uColor;
void main() { FragColor = vec4(uColor, 1.0); }
)GLSL";

// ---- GL helpers ----
static GLuint makeShader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok=0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if(!ok){
        GLint len=0; glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len);
        glGetShaderInfoLog(sh, len, nullptr, log.data());
        std::fprintf(stderr, "[Shader] %s\n", log.data());
    }
    return sh;
}
static GLuint makeProgram(const char* vs, const char* fs) {
    GLuint v = makeShader(GL_VERTEX_SHADER, vs);
    GLuint f = makeShader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    GLint ok=0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if(!ok){
        GLint len=0; glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len);
        glGetProgramInfoLog(p, len, nullptr, log.data());
        std::fprintf(stderr, "[Link] %s\n", log.data());
    }
    return p;
}

// ---- Geometry generators ----
static void genCircle(int segments, std::vector<float>& v) {
    v.clear(); v.reserve(segments*2);
    for(int i=0;i<segments;i++){
        float t = float(i) / segments * 6.283185307179586f;
        v.push_back(std::cos(t));
        v.push_back(std::sin(t));
    }
}
static void genTicks(int count, float innerR, float outerR, std::vector<float>& v) {
    v.clear(); v.reserve(count*4);
    for(int i=0;i<count;i++){
        float t = float(i) / count * 6.283185307179586f;
        float ct=std::cos(t), st=std::sin(t);
        v.push_back(ct*innerR); v.push_back(st*innerR);
        v.push_back(ct*outerR); v.push_back(st*outerR);
    }
}
static void genUnitHand(std::vector<float>& v) {
    // Rectangle from (−0.5,0) to (0.5,1) — will scale/rotate in shader
    const float data[] = {
        -0.5f, 0.0f,  0.5f, 0.0f,  0.5f, 1.0f,
        -0.5f, 0.0f,  0.5f, 1.0f, -0.5f, 1.0f
    };
    v.assign(data, data+12);
}
static void genDisc(int segments, std::vector<float>& v) {
    v.clear(); v.reserve((segments+2)*2);
    v.push_back(0.0f); v.push_back(0.0f);
    for(int i=0;i<=segments;i++){
        float t = float(i) / segments * 6.283185307179586f;
        v.push_back(std::cos(t)); v.push_back(std::sin(t));
    }
}

// ---- VAO/VBO wrapper ----
struct Mesh {
    GLuint vao=0, vbo=0; GLsizei count=0; GLenum mode=GL_TRIANGLES;
    void destroy(){ if(vbo) glDeleteBuffers(1,&vbo); if(vao) glDeleteVertexArrays(1,&vao); vao=vbo=0; }
    void draw() const { glBindVertexArray(vao); glDrawArrays(mode, 0, count); glBindVertexArray(0); }
};
static Mesh makeMesh(const std::vector<float>& verts, GLenum mode) {
    Mesh m; m.mode=mode; m.count=(GLsizei)(verts.size()/2);
    glGenVertexArrays(1,&m.vao);
    glGenBuffers(1,&m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);
    glBindVertexArray(0);
    return m;
}

static double nowSeconds(){
    using namespace std::chrono;
    auto tp = system_clock::now();
    auto s  = time_point_cast<seconds>(tp);
    return (double)s.time_since_epoch().count()
         + duration<double>(tp - s).count();
}

int main(){
    // --- GLFW setup (Core profile 3.2, required on macOS) ---
    if(!glfwInit()){ std::fprintf(stderr,"GLFW init failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,2);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GL_TRUE);
    glfwWindowHint(GLFW_SAMPLES,4);

    GLFWwindow* win = glfwCreateWindow(700,700,"2D Analog Clock",nullptr,nullptr);
    if(!win){ glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1); // vsync

    // --- Shaders & uniforms ---
    GLuint prog = makeProgram(kVS, kFS);
    GLint uAngle = glGetUniformLocation(prog,"uAngle");
    GLint uScale = glGetUniformLocation(prog,"uScale");
    GLint uTrans = glGetUniformLocation(prog,"uTranslate");
    GLint uColor = glGetUniformLocation(prog,"uColor");

    // --- Geometry ---
    std::vector<float> tmp;
    genCircle(256, tmp);             Mesh circle = makeMesh(tmp, GL_LINE_LOOP);
    genTicks(60,0.88f,0.95f,tmp);    Mesh minT   = makeMesh(tmp, GL_LINES);
    genTicks(12,0.80f,0.95f,tmp);    Mesh hrT    = makeMesh(tmp, GL_LINES);
    genUnitHand(tmp);                 Mesh hand   = makeMesh(tmp, GL_TRIANGLES);
    genDisc(40, tmp);                 Mesh dot    = makeMesh(tmp, GL_TRIANGLE_FAN);

    glEnable(GL_MULTISAMPLE);
    glClearColor(1,1,1,1); // white

    while(!glfwWindowShouldClose(win)){
        glfwPollEvents();

        // Local time → angles (smooth)
        double tnow = nowSeconds();
        std::time_t tt = (std::time_t)tnow;
        std::tm lt{};
    #if defined(_WIN32)
        localtime_s(&lt,&tt);
    #else
        lt = *std::localtime(&tt);
    #endif
        double frac  = tnow - std::floor(tnow);
        double s     = lt.tm_sec + frac;
        double m     = lt.tm_min + s/60.0;
        double h     = (lt.tm_hour % 12) + m/60.0;

        const double TAU = 6.283185307179586;
        auto toAngle = [&](double f)->float { return float(-TAU*f + TAU*0.25); }; // 12 up, clockwise+

        float aS = toAngle(s/60.0);
        float aM = toAngle(m/60.0);
        float aH = toAngle(h/12.0);

        int W,H; glfwGetFramebufferSize(win,&W,&H);
        glViewport(0,0,W,H);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(prog);
        glUniform2f(uTrans, 0.0f, 0.0f);
        glUniform1f(uAngle, 0.0f);
        glUniform3f(uColor, 0.0f, 0.0f, 0.0f); // black

        // Dial
        glUniform2f(uScale, 0.90f, 0.90f); circle.draw();

        // Ticks
        glUniform2f(uScale, 1.0f, 1.0f);
        glLineWidth(1.0f); minT.draw();
        glLineWidth(2.0f); hrT.draw();

        // Hands
        glUniform1f(uAngle, aH); glUniform2f(uScale, 0.06f, 0.55f); hand.draw();
        glUniform1f(uAngle, aM); glUniform2f(uScale, 0.04f, 0.80f); hand.draw();
        glUniform1f(uAngle, aS); glUniform2f(uScale, 0.02f, 0.88f); hand.draw();

        // Center cap
        glUniform1f(uAngle, 0.0f);
        glUniform2f(uScale, 0.03f, 0.03f); dot.draw();

        glfwSwapBuffers(win);
    }

    dot.destroy(); hand.destroy(); hrT.destroy(); minT.destroy(); circle.destroy();
    glDeleteProgram(prog);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
