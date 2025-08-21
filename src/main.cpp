// src/main.cpp
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

// ----------- Shaders (GLSL 1.50 core for macOS OpenGL 3.2) -----------
static const char* VS_SRC = R"GLSL(
#version 150 core
in vec2 aPos;
uniform float uAngle;      // radians, clockwise positive
uniform vec2  uScale;      // NDC scale
uniform vec2  uTrans;      // NDC translate
void main(){
    float c = cos(uAngle), s = sin(uAngle);
    vec2 p = vec2(c*aPos.x - s*aPos.y, s*aPos.x + c*aPos.y);
    p = p * uScale + uTrans;
    gl_Position = vec4(p, 0.0, 1.0);
}
)GLSL";

static const char* FS_SRC = R"GLSL(
#version 150 core
out vec4 FragColor;
uniform vec3 uColor;
void main(){ FragColor = vec4(uColor, 1.0); }
)GLSL";

// ----------- GL helpers -----------
static GLuint makeShader(GLenum type, const char* src){
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
static GLuint makeProgram(const char* vs, const char* fs){
    GLuint v = makeShader(GL_VERTEX_SHADER, vs);
    GLuint f = makeShader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    // GLSL 1.50 core doesn't support layout(location=..)
    glBindAttribLocation(p, 0, "aPos");
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok=0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if(!ok){
        GLint len=0; glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len);
        glGetProgramInfoLog(p, len, nullptr, log.data());
        std::fprintf(stderr, "[Link] %s\n", log.data());
    }
    return p;
}

// ----------- Simple Mesh wrapper (triangles) -----------
struct Mesh {
    GLuint vao=0, vbo=0;
    GLsizei count=0;
    void init(const std::vector<float>& pts){
        count = (GLsizei)(pts.size()/2);
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, pts.size()*sizeof(float), pts.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), (void*)0);
        glBindVertexArray(0);
    }
    void draw() const {
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, count);
        glBindVertexArray(0);
    }
    void destroy(){
        if(vbo) glDeleteBuffers(1,&vbo);
        if(vao) glDeleteVertexArrays(1,&vao);
        vbo=vao=0;
    }
};

// ----------- Geometry generators (all centered at pivot) -----------
static void addTri(std::vector<float>& v, float x1,float y1,float x2,float y2,float x3,float y3){
    v.insert(v.end(), {x1,y1, x2,y2, x3,y3});
}
static void addBox(std::vector<float>& v, float x0,float y0,float x1,float y1){
    // two triangles
    addTri(v, x0,y0, x1,y0, x1,y1);
    addTri(v, x0,y0, x1,y1, x0,y1);
}

// filled disc centered at origin, unit radius
static void genDiscFan(int seg, std::vector<float>& v){
    v.clear();
    for(int i=0;i<seg;i++){
        float a1 = 2.0f*M_PI*(i    /(float)seg);
        float a2 = 2.0f*M_PI*((i+1)/(float)seg);
        addTri(v, 0,0, std::cos(a1),std::sin(a1), std::cos(a2),std::sin(a2));
    }
}
// ring (triangle strip unrolled to triangles)
static void genRing(int seg, float r0, float r1, std::vector<float>& v){
    v.clear();
    for(int i=0;i<seg;i++){
        float a1 = 2.0f*M_PI*(i    /(float)seg);
        float a2 = 2.0f*M_PI*((i+1)/(float)seg);
        float c1=std::cos(a1), s1=std::sin(a1);
        float c2=std::cos(a2), s2=std::sin(a2);
        // two tris per segment
        addTri(v, c1*r0,s1*r0, c1*r1,s1*r1, c2*r1,s2*r1);
        addTri(v, c1*r0,s1*r0, c2*r1,s2*r1, c2*r0,s2*r0);
    }
}

// Numeral glyphs (simple filled block digits 0-9 in [-0.5..0.5]^2)
static void genDigit(std::vector<float>& v, int d){
    v.clear();
    auto box = [&](float x0,float y0,float x1,float y1){ addBox(v,x0,y0,x1,y1); };
    switch(d){
        case 0: box(-0.45f, 0.35f, 0.45f, 0.55f);
                box(-0.45f,-0.55f, 0.45f,-0.35f);
                box(-0.45f,-0.55f,-0.25f, 0.55f);
                box( 0.25f,-0.55f, 0.45f, 0.55f); break;
        case 1: box( 0.15f,-0.55f, 0.35f, 0.55f); break;
        case 2: box(-0.45f, 0.35f, 0.45f, 0.55f);
                box( 0.25f, 0.15f, 0.45f, 0.35f);
                box(-0.45f,-0.05f, 0.45f, 0.15f);
                box(-0.45f,-0.55f,-0.25f,-0.35f);
                box(-0.45f,-0.55f, 0.45f,-0.35f); break;
        case 3: box(-0.45f, 0.35f, 0.45f, 0.55f);
                box( 0.25f, 0.15f, 0.45f, 0.35f);
                box(-0.45f,-0.05f, 0.45f, 0.15f);
                box( 0.25f,-0.35f, 0.45f,-0.15f);
                box(-0.45f,-0.55f, 0.45f,-0.35f); break;
        case 4: box(-0.45f, 0.05f,-0.25f, 0.55f);
                box( 0.25f, 0.05f, 0.45f, 0.55f);
                box(-0.45f,-0.05f, 0.45f, 0.15f);
                box( 0.25f,-0.55f, 0.45f,-0.05f); break;
        case 5: box(-0.45f, 0.35f, 0.45f, 0.55f);
                box(-0.45f, 0.15f,-0.25f, 0.35f);
                box(-0.45f,-0.05f, 0.45f, 0.15f);
                box( 0.25f,-0.35f, 0.45f,-0.15f);
                box(-0.45f,-0.55f, 0.45f,-0.35f); break;
        case 6: box(-0.45f, 0.35f, 0.45f, 0.55f);
                box(-0.45f,-0.05f,-0.25f, 0.35f);
                box(-0.45f,-0.05f, 0.45f, 0.15f);
                box( 0.25f,-0.35f, 0.45f,-0.15f);
                box(-0.45f,-0.55f, 0.45f,-0.35f); break;
        case 7: box(-0.45f, 0.35f, 0.45f, 0.55f);
                box( 0.25f,-0.55f, 0.45f, 0.35f); break;
        case 8: box(-0.45f, 0.35f, 0.45f, 0.55f);
                box(-0.45f,-0.05f, 0.45f, 0.15f);
                box(-0.45f,-0.55f, 0.45f,-0.35f);
                box(-0.45f,-0.55f,-0.25f, 0.55f);
                box( 0.25f,-0.55f, 0.45f, 0.55f); break;
        case 9: box(-0.45f, 0.35f, 0.45f, 0.55f);
                box(-0.45f, 0.15f,-0.25f, 0.55f);
                box(-0.45f,-0.05f, 0.45f, 0.15f);
                box( 0.25f,-0.55f, 0.45f,-0.15f); break;
    }
}

// ----- Hands (small/clean to match the reference look) -----
static void genHourHand(std::vector<float>& v){
    v.clear();
    const float L    = 0.50f;  // shorter reach
    const float W    = 0.10f;  // stem width
    const float TAIL = 0.06f;  // short tail
    // stem rectangle
    addBox(v, -0.5f*W, 0.0f,  0.5f*W, L);
    // tiny tail
    addBox(v, -0.5f*W, -TAIL, 0.5f*W, 0.0f);
}

static void genMinuteHand(std::vector<float>& v){
    v.clear();
    const float L    = 0.72f;  // ends before numerals
    const float W    = 0.06f;
    const float TAIL = 0.08f;
    // base rectangle
    addBox(v, -0.5f*W, 0.0f,  0.5f*W, L-0.10f);
    // tapered tip
    addTri(v, -0.45f*W, L-0.10f,  0.45f*W, L-0.10f,  0.0f, L);
    // tail
    addBox(v, -0.5f*W, -TAIL, 0.5f*W, 0.0f);
}

static void genSecondHand(std::vector<float>& v){
    v.clear();
    const float L      = 0.82f;   // shorter needle
    const float W      = 0.018f;  // thin
    const float TAIL_L = 0.15f;
    const float HUB_R  = 0.030f;
    // needle
    addBox(v, -0.5f*W, 0.0f, 0.5f*W, L);
    // tail
    addBox(v, -0.5f*W, -TAIL_L, 0.5f*W, 0.0f);
    // hub disc (filled)
    std::vector<float> fan; genDiscFan(32, fan);
    for(size_t i=0;i<fan.size(); i+=6){
        addTri(v, fan[i+0]*HUB_R, fan[i+1]*HUB_R,
                  fan[i+2]*HUB_R, fan[i+3]*HUB_R,
                  fan[i+4]*HUB_R, fan[i+5]*HUB_R);
    }
}

// ---------- Time helper (local time, ticking seconds) ----------
static double nowSeconds(){
    using namespace std::chrono;
    auto tp = system_clock::now();
    auto s  = time_point_cast<seconds>(tp);
    return (double)s.time_since_epoch().count()
         + duration<double>(tp - s).count();
}

// ---------- Main ----------
int main(){
    if(!glfwInit()){ std::fprintf(stderr,"GLFW init failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,2);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GL_TRUE); // macOS
    glfwWindowHint(GLFW_SAMPLES,4);

    GLFWwindow* win = glfwCreateWindow(720,720, "2D Clock", nullptr, nullptr);
    if(!win){ glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    GLuint prog = makeProgram(VS_SRC, FS_SRC);
    glUseProgram(prog);
    GLint uAng = glGetUniformLocation(prog, "uAngle");
    GLint uSc  = glGetUniformLocation(prog, "uScale");
    GLint uTr  = glGetUniformLocation(prog, "uTrans");
    GLint uCol = glGetUniformLocation(prog, "uColor");

    // Build meshes
    std::vector<float> tmp;

    // Dial elements
    genRing(256, 0.98f, 0.86f, tmp);  Mesh bezel; bezel.init(tmp);     // outer ring
    genDiscFan(128, tmp);             Mesh dial;  dial.init(tmp);      // white face (unit disc)
    genRing(256, 0.84f, 0.82f, tmp);  Mesh chap;  chap.init(tmp);      // inner chapter ring

    // Ticks
    // (drawn as lines would be thin on macOS; we’ll leave them out here for simplicity)

    // Hands
    genHourHand(tmp);   Mesh mh; mh.init(tmp);
    genMinuteHand(tmp); Mesh mm; mm.init(tmp);
    genSecondHand(tmp); Mesh ms; ms.init(tmp);

    // Numerals: small & tucked near the chapter ring
    Mesh digits[13];
    for(int n=1;n<=12;n++){ genDigit(tmp, n%10); digits[n].init(tmp); }

    glEnable(GL_MULTISAMPLE);
    glClearColor(1,1,1,1);

    const double TAU = 6.28318530718;

    auto numeralAngle = [&](int n)->float{
        // 12 at top; clockwise
        int idx = n % 12; // 12 -> 0
        return float(-TAU*(idx/12.0f) + TAU*0.25f);
    };

    while(!glfwWindowShouldClose(win)){
        glfwPollEvents();

        // Local time
        double tnow = nowSeconds();
        std::time_t tt = (std::time_t)tnow;
        std::tm lt{};
    #if defined(_WIN32)
        localtime_s(&lt, &tt);
    #else
        lt = *std::localtime(&tt);
    #endif
        int    s_i = lt.tm_sec;                  // tick
        double s   = double(s_i);
        double m   = lt.tm_min + s/60.0;
        double h   = (lt.tm_hour % 12) + m/60.0;

        auto toA = [&](double f)->float { return float(-TAU*f + TAU*0.25f); };
        float aS = toA(s/60.0);
        float aM = toA(m/60.0);
        float aH = toA(h/12.0);

        int W,H; glfwGetFramebufferSize(win,&W,&H);
        glViewport(0,0,W,H);
        glClear(GL_COLOR_BUFFER_BIT);

        // ---- Draw dial ----
        glUniform1f(uAng, 0.0f);
        glUniform2f(uTr,  0.0f, 0.0f);

        // outer bezel (brownish)
        glUniform3f(uCol, 0.42f, 0.22f, 0.12f);
        glUniform2f(uSc,  1.0f, 1.0f);
        bezel.draw();

        // white face
        glUniform3f(uCol, 1.0f, 1.0f, 1.0f);
        glUniform2f(uSc,  0.86f, 0.86f);
        dial.draw();

        // inner ring
        glUniform3f(uCol, 0.75f, 0.75f, 0.75f);
        glUniform2f(uSc,  1.0f, 1.0f);
        chap.draw();

        // ---- Numerals ----
        float rNum = 0.73f;   // tuck near inner ring
        float sNum = 0.10f;   // small
        glUniform3f(uCol, 0.0f, 0.0f, 0.0f);
        for(int n=1;n<=12;n++){
            float ang = numeralAngle(n);
            float cx = std::cos(ang)*rNum;
            float cy = std::sin(ang)*rNum;
            glUniform1f(uAng, 0.0f);       // keep upright
            glUniform2f(uSc,  sNum, sNum);
            glUniform2f(uTr,  cx, cy);
            digits[n].draw();
        }

        // ---- Hands ----
        glUniform2f(uTr, 0.0f, 0.0f);

        // hour (black)
        glUniform3f(uCol, 0,0,0);
        glUniform1f(uAng, aH);
        glUniform2f(uSc,  1.0f, 1.0f);
        mh.draw();

        // minute (black)
        glUniform1f(uAng, aM);
        glUniform2f(uSc,  1.0f, 1.0f);
        mm.draw();

        // second (gold)
        glUniform3f(uCol, 0.80f, 0.70f, 0.35f);
        glUniform1f(uAng, aS);
        glUniform2f(uSc,  1.0f, 1.0f);
        ms.draw();

        glfwSwapBuffers(win);
    }

    for(int n=1;n<=12;n++) digits[n].destroy();
    ms.destroy(); mm.destroy(); mh.destroy();
    chap.destroy(); dial.destroy(); bezel.destroy();
    glDeleteProgram(prog);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
