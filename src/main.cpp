#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/gl3.h>
#include <GLFW/glfw3.h>

#include <vector>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <ctime>

// ====== Shaders (GLSL 1.50 core) ======
static const char* kVS = R"GLSL(
#version 150 core
in vec2 aPos;
uniform float uAngle;      // radians; clockwise positive (we flip inside)
uniform vec2  uScale;      // per-draw scale
uniform vec2  uTranslate;  // per-draw translate
void main(){
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
void main(){ FragColor = vec4(uColor, 1.0); }
)GLSL";

// ====== GL helpers ======
static GLuint makeShader(GLenum type, const char* src){
    GLuint sh = glCreateShader(type);
    glShaderSource(sh,1,&src,nullptr);
    glCompileShader(sh);
    GLint ok=0; glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
    if(!ok){
        GLint len=0; glGetShaderiv(sh,GL_INFO_LOG_LENGTH,&len);
        std::vector<char> log(len); glGetShaderInfoLog(sh,len,nullptr,log.data());
        std::fprintf(stderr,"[Shader] %s\n", log.data());
    }
    return sh;
}
static GLuint makeProgram(const char* vs, const char* fs){
    GLuint v = makeShader(GL_VERTEX_SHADER,vs);
    GLuint f = makeShader(GL_FRAGMENT_SHADER,fs);
    GLuint p = glCreateProgram();
    glAttachShader(p,v); glAttachShader(p,f);
    glBindAttribLocation(p,0,"aPos"); // (no layout() in GLSL 150)
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){
        GLint len=0; glGetProgramiv(p,GL_INFO_LOG_LENGTH,&len);
        std::vector<char> log(len); glGetProgramInfoLog(p,len,nullptr,log.data());
        std::fprintf(stderr,"[Link] %s\n", log.data());
    }
    return p;
}

// ====== Mesh wrapper ======
struct Mesh {
    GLuint vao=0,vbo=0; GLsizei count=0; GLenum mode=GL_TRIANGLES;
    void draw() const { glBindVertexArray(vao); glDrawArrays(mode,0,count); glBindVertexArray(0); }
    void destroy(){ if(vbo) glDeleteBuffers(1,&vbo); if(vao) glDeleteVertexArrays(1,&vao); vao=vbo=0; }
};
static Mesh makeMesh(const std::vector<float>& v, GLenum mode){
    Mesh m; m.mode=mode; m.count=(GLsizei)(v.size()/2);
    glGenVertexArrays(1,&m.vao);
    glGenBuffers(1,&m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER,m.vbo);
    glBufferData(GL_ARRAY_BUFFER,v.size()*sizeof(float),v.data(),GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);
    glBindVertexArray(0);
    return m;
}

// ====== Geometry generators ======
static void genCircleLine(int seg, std::vector<float>& v){ // line loop (unit radius)
    v.clear(); v.reserve(seg*2);
    for(int i=0;i<seg;i++){ float t=i*(6.28318530718f/seg); v.push_back(std::cos(t)); v.push_back(std::sin(t)); }
}
static void genRing(int seg, float r0, float r1, std::vector<float>& v){ // triangle strip ring
    v.clear(); v.reserve((seg+1)*4);
    for(int i=0;i<=seg;i++){
        float t=i*(6.28318530718f/seg), c=std::cos(t), s=std::sin(t);
        v.push_back(c*r0); v.push_back(s*r0);
        v.push_back(c*r1); v.push_back(s*r1);
    }
}
static void genTicks(int count, float innerR, float outerR, std::vector<float>& v){
    v.clear(); v.reserve(count*4);
    for(int i=0;i<count;i++){
        float t=i*(6.28318530718f/count), c=std::cos(t), s=std::sin(t);
        v.push_back(c*innerR); v.push_back(s*innerR);
        v.push_back(c*outerR); v.push_back(s*outerR);
    }
}
static void genDiscFan(int seg, std::vector<float>& v){ // unit disc (triangle fan)
    v.clear(); v.reserve((seg+2)*2);
    v.push_back(0); v.push_back(0);
    for(int i=0;i<=seg;i++){ float t=i*(6.28318530718f/seg); v.push_back(std::cos(t)); v.push_back(std::sin(t)); }
}
// Spade hour hand: rectangle stem + circular bulb near tip
static void genSpadeHour(std::vector<float>& v){
    v.clear();
    auto addTri = [&](float x1,float y1,float x2,float y2,float x3,float y3){
        v.insert(v.end(),{x1,y1,x2,y2,x3,y3});
    };
    // Stem (width ~0.6, length 0.72 of unit)
    float w=0.60f, L=0.72f;
    addTri(-0.5f*w,0,  0.5f*w,0,  0.5f*w,L);
    addTri(-0.5f*w,0,  0.5f*w,L, -0.5f*w,L);
    // Bulb (circle segment) centered a bit before tip
    std::vector<float> fan; genDiscFan(48,fan); // unit radius
    // Reuse as a scaled/translated fan:
    // We'll draw this whole mesh as triangles; append transformed fan triangles (skip center tri winding details by re‑triangulating)
    // Build triangles from center(0,0) to edge points
    float r=0.40f, cy=L+0.15f;
    for(int i=1;i<(int)fan.size()/2 -1; ++i){
        float x0=0, y0=cy;
        float x1=fan[i*2+0]*r, y1=fan[i*2+1]*r + cy;
        float x2=fan[(i+1)*2+0]*r, y2=fan[(i+1)*2+1]*r + cy;
        addTri(x0,y0,x1,y1,x2,y2);
    }
}
// Minute hand: long slim pointer (stem + small triangular tip)
static void genPointerMinute(std::vector<float>& v){
    v.clear();
    auto addTri=[&](float a,float b,float c,float d,float e,float f){ v.insert(v.end(),{a,b,c,d,e,f}); };
    float w=0.30f, L=0.95f;
    addTri(-0.5f*w,0,  0.5f*w,0,  0.5f*w,L-0.10f);
    addTri(-0.5f*w,0,  0.5f*w,L-0.10f, -0.5f*w,L-0.10f);
    // pointer tip
    addTri(-0.35f*w,L-0.10f,  0.35f*w,L-0.10f,  0.0f,L);
}
// Second hand: ultra-thin long rectangle (we’ll color it gold)
static void genSecondThin(std::vector<float>& v){
    const float w=0.12f, L=1.00f;
    v = { -0.5f*w,0,  0.5f*w,0,  0.5f*w,L,
          -0.5f*w,0,  0.5f*w,L, -0.5f*w,L };
}

// ====== Time helper ======
static double nowSeconds(){
    using namespace std::chrono;
    auto tp = std::chrono::system_clock::now();
    auto s  = std::chrono::time_point_cast<std::chrono::seconds>(tp);
    return (double)s.time_since_epoch().count()
         + std::chrono::duration<double>(tp - s).count();
}

int main(){
    // --- Window / context ---
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

    // --- Program & uniforms ---
    GLuint prog = makeProgram(kVS,kFS);
    GLint uAngle = glGetUniformLocation(prog,"uAngle");
    GLint uScale = glGetUniformLocation(prog,"uScale");
    GLint uTrans = glGetUniformLocation(prog,"uTranslate");
    GLint uColor = glGetUniformLocation(prog,"uColor");

    // --- Geometry meshes ---
    std::vector<float> tmp;

    // Bezel ring (brownish outer ring like photo) -> use ring strip
    genRing(256, 0.98f, 0.86f, tmp);           Mesh bezel  = makeMesh(tmp, GL_TRIANGLE_STRIP);
    // White dial disc (filled) -> re-use ring with center fill using fan
    genDiscFan(128, tmp);                      Mesh dial   = makeMesh(tmp, GL_TRIANGLE_FAN);

    // Chapter ring (thin inner gray ring)
    genRing(256, 0.84f, 0.82f, tmp);           Mesh innerRing = makeMesh(tmp, GL_TRIANGLE_STRIP);

    // Minute ticks (60) short; Hour ticks (12) longer & thicker
    genTicks(60, 0.82f, 0.88f, tmp);           Mesh minTicks = makeMesh(tmp, GL_LINES);
    genTicks(12, 0.78f, 0.90f, tmp);           Mesh hrTicks  = makeMesh(tmp, GL_LINES);

    // Hands
    genSpadeHour(tmp);                         Mesh hourHand = makeMesh(tmp, GL_TRIANGLES);
    genPointerMinute(tmp);                     Mesh minHand  = makeMesh(tmp, GL_TRIANGLES);
    genSecondThin(tmp);                        Mesh secHand  = makeMesh(tmp, GL_TRIANGLES);

    // Center cap
    genDiscFan(40, tmp);                       Mesh cap      = makeMesh(tmp, GL_TRIANGLE_FAN);

    glEnable(GL_MULTISAMPLE);
    glClearColor(1,1,1,1);

    while(!glfwWindowShouldClose(win)){
        glfwPollEvents();

        // --- Local time (smooth) ---
        double tnow = nowSeconds();
        std::time_t tt = (std::time_t)tnow;
        std::tm lt{};
    #if defined(_WIN32)
        localtime_s(&lt,&tt);
    #else
        lt = *std::localtime(&tt);
    #endif
        double frac = tnow - std::floor(tnow);
        double secs = lt.tm_sec + frac;
        double mins = lt.tm_min + secs/60.0;
        double hour = (lt.tm_hour%12) + mins/60.0;

        const double TAU = 6.28318530718;
        auto toA = [&](double f)->float { return float(-TAU*f + TAU*0.25); }; // 12 up

        float aS = toA(secs/60.0);
        float aM = toA(mins/60.0);
        float aH = toA(hour/12.0);

        int W,H; glfwGetFramebufferSize(win,&W,&H);
        glViewport(0,0,W,H);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(prog);
        glUniform2f(uTrans, 0,0);

        // ---- Draw bezel (brown), dial (white), inner ring (light gray) ----
        glUniform1f(uAngle, 0.0f);

        // Outer bezel ring (brown copper tone)
        glUniform3f(uColor, 0.42f, 0.22f, 0.12f);
        glUniform2f(uScale, 1.0f, 1.0f);  bezel.draw();

        // White dial
        glUniform3f(uColor, 1.0f, 1.0f, 1.0f);
        glUniform2f(uScale, 0.86f, 0.86f); dial.draw();

        // Inner subtle ring
        glUniform3f(uColor, 0.75f, 0.75f, 0.75f);
        glUniform2f(uScale, 1.0f, 1.0f);  innerRing.draw();

        // Ticks
        glUniform3f(uColor, 0.0f, 0.0f, 0.0f);
        glLineWidth(1.2f);  minTicks.draw();
        glLineWidth(2.2f);  hrTicks.draw();

        // Hands (black)
        glUniform3f(uColor, 0,0,0);
        glUniform1f(uAngle, aH); glUniform2f(uScale, 0.06f, 0.55f); hourHand.draw();
        glUniform1f(uAngle, aM); glUniform2f(uScale, 0.04f, 0.88f); minHand.draw();

        // Second hand (gold)
        glUniform3f(uColor, 0.80f, 0.70f, 0.35f);
        glUniform1f(uAngle, aS); glUniform2f(uScale, 0.02f, 0.92f); secHand.draw();

        // Center cap (black disc)
        glUniform3f(uColor, 0,0,0);
        glUniform1f(uAngle, 0.0f); glUniform2f(uScale, 0.035f, 0.035f); cap.draw();

        glfwSwapBuffers(win);
    }

    cap.destroy(); secHand.destroy(); minHand.destroy(); hourHand.destroy();
    hrTicks.destroy(); minTicks.destroy(); innerRing.destroy(); dial.destroy(); bezel.destroy();
    glDeleteProgram(prog);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
