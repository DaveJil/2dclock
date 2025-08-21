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

// ================= Shaders (GLSL 1.50 core) =================
static const char* VS_SRC = R"GLSL(
#version 150 core
in vec2 aPos;
uniform float uAngle;   // radians, clockwise positive
uniform vec2  uScale;   // NDC scale
uniform vec2  uTrans;   // NDC translate
void main(){
    float c = cos(uAngle), s = sin(uAngle);
    vec2 p = vec2(c*aPos.x - s*aPos.y, s*aPos.x + c*aPos.y);
    p = p*uScale + uTrans;
    gl_Position = vec4(p, 0.0, 1.0);
}
)GLSL";

static const char* FS_SRC = R"GLSL(
#version 150 core
out vec4 FragColor;
uniform vec3 uColor;
void main(){ FragColor = vec4(uColor, 1.0); }
)GLSL";

// ================= GL helpers =================
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
    glBindAttribLocation(p, 0, "aPos"); // GLSL 150 core: bind before link
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

// ================= Mesh wrapper (Triangles) =================
struct Mesh {
    GLuint vao=0, vbo=0;
    GLsizei count=0;
    void init(const std::vector<float>& verts){
        count = (GLsizei)(verts.size()/2);
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
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
        if(vbo) glDeleteBuffers(1, &vbo);
        if(vao) glDeleteVertexArrays(1, &vao);
        vbo = vao = 0;
    }
};

// ================= Geometry utils =================
static void addTri(std::vector<float>& v, float x1,float y1,float x2,float y2,float x3,float y3){
    v.insert(v.end(), {x1,y1, x2,y2, x3,y3});
}
static void addBox(std::vector<float>& v, float x0,float y0,float x1,float y1){
    addTri(v, x0,y0, x1,y0, x1,y1);
    addTri(v, x0,y0, x1,y1, x0,y1);
}
static void genDisc(std::vector<float>& v, int seg=128, float r=1.0f){
    v.clear();
    for(int i=0;i<seg;i++){
        float a0 = 2.0f*M_PI*(i/(float)seg);
        float a1 = 2.0f*M_PI*((i+1)/(float)seg);
        addTri(v, 0,0, r*std::cos(a0), r*std::sin(a0), r*std::cos(a1), r*std::sin(a1));
    }
}
static void genRing(std::vector<float>& v, int seg, float r0, float r1){
    v.clear();
    for(int i=0;i<seg;i++){
        float a0 = 2.0f*M_PI*(i/(float)seg);
        float a1 = 2.0f*M_PI*((i+1)/(float)seg);
        float c0=std::cos(a0), s0=std::sin(a0);
        float c1=std::cos(a1), s1=std::sin(a1);
        // two tris per slice
        addTri(v, c0*r0,s0*r0, c0*r1,s0*r1, c1*r1,s1*r1);
        addTri(v, c0*r0,s0*r0, c1*r1,s1*r1, c1*r0,s1*r0);
    }
}
// radial tick (filled quad) centered on angle 'a', from innerR..outerR with tangential half-width 'halfW'
static void addRadialBar(std::vector<float>& v, float a, float innerR, float outerR, float halfW){
    float ca=std::cos(a), sa=std::sin(a);
    float tx=-sa, ty=ca; // tangent unit
    // four corners (inner/outer, left/right)
    float ix=ca*innerR, iy=sa*innerR;
    float ox=ca*outerR, oy=sa*outerR;
    float ilx=ix - tx*halfW, ily=iy - ty*halfW;
    float irx=ix + tx*halfW, iry=iy + ty*halfW;
    float olx=ox - tx*halfW, oly=oy - ty*halfW;
    float orx=ox + tx*halfW, ory=oy + ty*halfW;
    // two triangles
    addTri(v, ilx,ily, irx,iry, orx,ory);
    addTri(v, ilx,ily, orx,ory, olx,oly);
}
static void genTicksQuads(std::vector<float>& v, int count, float innerR, float outerR, float width){
    v.clear();
    float halfW = width*0.5f;
    for(int i=0;i<count;i++){
        float a = 2.0f*M_PI*(i/(float)count);
        addRadialBar(v, a, innerR, outerR, halfW);
    }
}

// ================= Hands (resized to fit) =================
// Hour: short spade-like rectangle + short tail
static void genHourHand(std::vector<float>& v){
    v.clear();
    const float L    = 0.50f;  // reach
    const float W    = 0.10f;  // width
    const float TAIL = 0.06f;  // tail
    addBox(v, -0.5f*W, 0.0f,  0.5f*W, L);
    addBox(v, -0.5f*W,-TAIL,  0.5f*W, 0.0f);
}
// Minute: longer, tapered pointer + small tail
static void genMinuteHand(std::vector<float>& v){
    v.clear();
    const float L    = 0.72f;
    const float W    = 0.06f;
    const float TAIL = 0.08f;
    addBox(v, -0.5f*W, 0.0f,  0.5f*W, L-0.10f);
    addTri(v, -0.45f*W, L-0.10f,  0.45f*W, L-0.10f,  0.0f, L);
    addBox(v, -0.5f*W,-TAIL,  0.5f*W, 0.0f);
}
// Second: thin needle + counterweight tail + small hub
static void genSecondHand(std::vector<float>& v){
    v.clear();
    const float L      = 0.82f;
    const float W      = 0.018f;
    const float TAIL_L = 0.15f;
    const float HUB_R  = 0.030f;
    addBox(v, -0.5f*W, 0.0f,  0.5f*W, L);          // needle
    addBox(v, -0.5f*W,-TAIL_L, 0.5f*W, 0.0f);      // tail
    // hub disc
    std::vector<float> fan; genDisc(fan, 32, HUB_R);
    v.insert(v.end(), fan.begin(), fan.end());
}

// ================= Numerals (filled block digits) =================
// Compact, high-contrast block numerals in [-0.5..0.5]^2
static void genDigitMesh(std::vector<float>& v, int d){
    v.clear();
    auto box = [&](float x0,float y0,float x1,float y1){ addBox(v,x0,y0,x1,y1); };
    switch(d){
        case 0: box(-0.45f, 0.35f, 0.45f, 0.55f);
                box(-0.45f,-0.55f, 0.45f,-0.35f);
                box(-0.45f,-0.55f,-0.25f, 0.55f);
                box( 0.25f,-0.55f, 0.45f, 0.55f); break;
        case 1: box( 0.10f,-0.55f, 0.30f, 0.55f); break; // slimmer '1' for clarity
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
                box(-0.45f,-0.55f, 0.45f,-0.35f);
                // add a small vertical to clarify '6'
                box(-0.45f, -0.35f, -0.25f, -0.15f); break;
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

// Build meshes for numerals 1..12, composing tens+ones for 10,11,12
struct NumeralMesh { Mesh mesh; };
static void buildNumerals(std::vector<NumeralMesh>& out){
    out.resize(13); // index 1..12
    for(int n=1;n<=12;n++){
        std::vector<float> verts, tens, ones;
        if(n<10){
            genDigitMesh(ones, n);
            // scale down a touch horizontally so two-digit looks balanced
            // (we’ll rely on global scale at draw, so just keep as-is)
            verts.insert(verts.end(), ones.begin(), ones.end());
        }else{
            int t = n/10, o = n%10;
            genDigitMesh(tens, t);
            genDigitMesh(ones, o);
            // place tens left, ones right with small gap
            const float dx = 0.75f; // center offset
            const float gap= 0.10f; // between digits
            // tens -> shift left
            for(size_t i=0;i<tens.size(); i+=2){ verts.push_back(tens[i] - dx - gap); verts.push_back(tens[i+1]); }
            // ones -> shift right
            for(size_t i=0;i<ones.size(); i+=2){ verts.push_back(ones[i] + dx + gap); verts.push_back(ones[i+1]); }
        }
        out[n].mesh.init(verts);
    }
}

// ================= Time helper =================
static double nowSeconds(){
    using namespace std::chrono;
    auto tp = system_clock::now();
    auto s  = time_point_cast<seconds>(tp);
    return (double)s.time_since_epoch().count()
         + duration<double>(tp - s).count();
}

// ================= Main =================
int main(){
    if(!glfwInit()){ std::fprintf(stderr,"GLFW init failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,2);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GL_TRUE);
    glfwWindowHint(GLFW_SAMPLES,4);

    GLFWwindow* win = glfwCreateWindow(800,800,"Analog Clock",nullptr,nullptr);
    if(!win){ glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    GLuint prog = makeProgram(VS_SRC, FS_SRC);
    glUseProgram(prog);
    GLint uAng = glGetUniformLocation(prog,"uAngle");
    GLint uSc  = glGetUniformLocation(prog,"uScale");
    GLint uTr  = glGetUniformLocation(prog,"uTrans");
    GLint uCol = glGetUniformLocation(prog,"uColor");

    // Dial geometry
    std::vector<float> v;
    genRing(v, 256, 0.98f, 0.86f); Mesh bezel;     bezel.init(v);   // outer bezel ring
    genDisc(v, 128, 1.0f);         Mesh face;      face.init(v);    // white dial disc
    genRing(v, 256, 0.84f, 0.82f); Mesh innerRing; innerRing.init(v); // chapter ring

    // Ticks (filled quads so they’re crisp on macOS)
    genTicksQuads(v, 60, 0.82f, 0.88f, 0.010f); Mesh minuteTicks; minuteTicks.init(v); // thin
    genTicksQuads(v, 12, 0.78f, 0.90f, 0.020f); Mesh hourTicks;   hourTicks.init(v);   // bold

    // Hands
    genHourHand(v);   Mesh hourHand;   hourHand.init(v);
    genMinuteHand(v); Mesh minuteHand; minuteHand.init(v);
    genSecondHand(v); Mesh secondHand; secondHand.init(v);

    // Numerals (1..12)
    std::vector<NumeralMesh> numerals; buildNumerals(numerals);

    glEnable(GL_MULTISAMPLE);
    glClearColor(1,1,1,1);

    const double TAU = 6.28318530718;
    auto numeralAngle = [&](int n)->float{
        int idx = n % 12; // 12→0
        return float(-TAU*(idx/12.0f) + TAU*0.25f);
    };

    while(!glfwWindowShouldClose(win)){
        glfwPollEvents();

        // local time (ticking seconds, smooth hour/minute)
        double tnow = nowSeconds();
        std::time_t tt = (std::time_t)tnow;
        std::tm lt{};
    #if defined(_WIN32)
        localtime_s(&lt,&tt);
    #else
        lt = *std::localtime(&tt);
    #endif
        int    s_i = lt.tm_sec;           // tick
        double s   = double(s_i);
        double m   = lt.tm_min + s/60.0;  // smooth minute
        double h   = (lt.tm_hour%12) + m/60.0; // smooth hour

        auto toA = [&](double f)->float { return float(-TAU*f + TAU*0.25f); };
        float aS = toA(s/60.0);
        float aM = toA(m/60.0);
        float aH = toA(h/12.0);

        int W,H; glfwGetFramebufferSize(win,&W,&H);
        glViewport(0,0,W,H);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(prog);

        // ---- Dial ----
        glUniform1f(uAng, 0.0f);
        glUniform2f(uTr,  0.0f, 0.0f);

        // Bezel (subtle brown)
        glUniform3f(uCol, 0.42f, 0.22f, 0.12f);
        glUniform2f(uSc,  1.0f, 1.0f); bezel.draw();

        // White dial (restore face)
        glUniform3f(uCol, 1.0f, 1.0f, 1.0f);
        glUniform2f(uSc,  0.86f, 0.86f); face.draw();

        // Chapter ring
        glUniform3f(uCol, 0.75f, 0.75f, 0.75f);
        glUniform2f(uSc,  1.0f, 1.0f); innerRing.draw();

        // Ticks
        glUniform3f(uCol, 0.0f, 0.0f, 0.0f);
        glUniform2f(uSc,  1.0f, 1.0f);
        minuteTicks.draw();
        hourTicks.draw();

        // ---- Numerals (upright) ----
        float rNum = 0.73f;  // tuck near inner ring
        float sNum = 0.10f;  // compact
        glUniform3f(uCol, 0.0f, 0.0f, 0.0f);
        for(int n=1;n<=12;n++){
            float ang = numeralAngle(n);
            float cx = std::cos(ang)*rNum;
            float cy = std::sin(ang)*rNum;
            glUniform1f(uAng, 0.0f);        // keep upright
            glUniform2f(uSc,  sNum, sNum);
            glUniform2f(uTr,  cx, cy);
            numerals[n].mesh.draw();
        }

        // ---- Hands ----
        glUniform2f(uTr, 0.0f, 0.0f);

        // Hour (black)
        glUniform3f(uCol, 0,0,0);
        glUniform1f(uAng, aH);
        glUniform2f(uSc,  1.0f, 1.0f);
        hourHand.draw();

        // Minute (black)
        glUniform1f(uAng, aM);
        minuteHand.draw();

        // Second (gold)
        glUniform3f(uCol, 0.80f, 0.70f, 0.35f);
        glUniform1f(uAng, aS);
        secondHand.draw();

        glfwSwapBuffers(win);
    }

    // cleanup
    for(int n=1;n<=12;n++) numerals[n].mesh.destroy();
    secondHand.destroy(); minuteHand.destroy(); hourHand.destroy();
    hourTicks.destroy(); minuteTicks.destroy();
    innerRing.destroy(); face.destroy(); bezel.destroy();
    glDeleteProgram(prog);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
