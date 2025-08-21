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

// ---------- Shaders (GLSL 1.50 core) ----------
static const char* kVS = R"GLSL(
#version 150 core
in vec2 aPos;
uniform float uAngle;      // radians; clockwise positive by convention
uniform vec2  uScale;      // NDC scale
uniform vec2  uTranslate;  // NDC translate
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

// ---------- GL helpers ----------
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
    glBindAttribLocation(p,0,"aPos"); // GLSL 150
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

// ---------- Mesh ----------
struct Mesh{
    GLuint vao=0,vbo=0; GLsizei count=0; GLenum mode=GL_TRIANGLES;
    void draw() const { glBindVertexArray(vao); glDrawArrays(mode,0,count); glBindVertexArray(0); }
    void destroy(){ if(vbo) glDeleteBuffers(1,&vbo); if(vao) glDeleteVertexArrays(1,&vao); vao=vbo=0; }
};
static Mesh makeMesh(const std::vector<float>& v, GLenum mode=GL_TRIANGLES){
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

// ---------- Simple shapes ----------
static void genRing(int seg, float r0, float r1, std::vector<float>& v){
    v.clear(); v.reserve((seg+1)*4);
    for(int i=0;i<=seg;i++){
        float t=i*(6.28318530718f/seg), c=std::cos(t), s=std::sin(t);
        v.push_back(c*r0); v.push_back(s*r0);
        v.push_back(c*r1); v.push_back(s*r1);
    }
}
static void genDiscFan(int seg, std::vector<float>& v){
    v.clear(); v.reserve((seg+2)*2);
    v.push_back(0); v.push_back(0);
    for(int i=0;i<=seg;i++){
        float t=i*(6.28318530718f/seg);
        v.push_back(std::cos(t)); v.push_back(std::sin(t));
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
static void genSpadeHour(std::vector<float>& v){
    v.clear();
    auto tri=[&](float a,float b,float c,float d,float e,float f){ v.insert(v.end(),{a,b,c,d,e,f}); };
    float w=0.60f, L=0.72f;
    tri(-0.5f*w,0,  0.5f*w,0,  0.5f*w,L);
    tri(-0.5f*w,0,  0.5f*w,L, -0.5f*w,L);
    std::vector<float> fan; genDiscFan(48,fan);
    float r=0.40f, cy=L+0.15f;
    for(int i=1;i<(int)fan.size()/2-1;i++){
        float x0=0, y0=cy;
        float x1=fan[i*2]*r,     y1=fan[i*2+1]*r + cy;
        float x2=fan[(i+1)*2]*r, y2=fan[(i+1)*2+1]*r + cy;
        tri(x0,y0,x1,y1,x2,y2);
    }
}
static void genPointerMinute(std::vector<float>& v){
    v.clear();
    auto tri=[&](float a,float b,float c,float d,float e,float f){ v.insert(v.end(),{a,b,c,d,e,f}); };
    float w=0.30f, L=0.95f;
    tri(-0.5f*w,0,  0.5f*w,0,  0.5f*w,L-0.10f);
    tri(-0.5f*w,0,  0.5f*w,L-0.10f, -0.5f*w,L-0.10f);
    tri(-0.35f*w,L-0.10f,  0.35f*w,L-0.10f,  0.0f,L);
}
static void genSecondThin(std::vector<float>& v){
    const float w=0.12f, L=1.00f;
    v = { -0.5f*w,0,  0.5f*w,0,  0.5f*w,L,
          -0.5f*w,0,  0.5f*w,L, -0.5f*w,L };
}

// ---------- Thick (filled) 7‑segment digits ----------
static void addQuad(std::vector<float>& v, float x1,float y1,float x2,float y2,float t){
    // rectangle centered on segment (x1,y1)->(x2,y2), thickness t
    float dx=x2-x1, dy=y2-y1;
    float len = std::sqrt(dx*dx+dy*dy); if(len==0) return;
    dx/=len; dy/=len;
    float px=-dy*(t*0.5f), py=dx*(t*0.5f);
    float ax=x1+px, ay=y1+py;
    float bx=x2+px, by=y2+py;
    float cx=x2-px, cy=y2-py;
    float dx2=x1-px,dy2=y1-py;
    v.insert(v.end(), { ax,ay, bx,by, cx,cy,  ax,ay, cx,cy, dx2,dy2 });
}
// segment endpoints in local digit space
static const float X0=-0.45f, X1=0.45f, Y0=-0.60f, Y1=0.60f, Ym=0.0f;
static void genDigitFilled(int d, float thickness, std::vector<float>& v){
    v.clear();
    auto A=[&](){ addQuad(v,X0,Y1, X1,Y1, thickness); }; // top
    auto B=[&](){ addQuad(v,X1,Y1, X1,Ym, thickness); }; // upper right
    auto C=[&](){ addQuad(v,X1,Ym, X1,Y0, thickness); }; // lower right
    auto D=[&](){ addQuad(v,X0,Y0, X1,Y0, thickness); }; // bottom
    auto E=[&](){ addQuad(v,X0,Ym, X0,Y0, thickness); }; // lower left
    auto F=[&](){ addQuad(v,X0,Y1, X0,Ym, thickness); }; // upper left
    auto G=[&](){ addQuad(v,X0,Ym, X1,Ym, thickness); }; // middle
    switch(d){
        case 0: A();B();C();D();E();F(); break;
        case 1: B();C(); break;
        case 2: A();B();G();E();D(); break;
        case 3: A();B();G();C();D(); break;
        case 4: F();G();B();C(); break;
        case 5: A();F();G();C();D(); break;
        case 6: A();F();G();E();D();C(); break;
        case 7: A();B();C(); break;
        case 8: A();B();C();D();E();F();G(); break;
        case 9: A();B();C();D();F();G(); break;
    }
}

struct Numeral { Mesh mesh; float width; };
static std::vector<Numeral> buildNumerals(){
    std::vector<Numeral> out(13);
    for(int n=1;n<=12;n++){
        std::vector<float> verts, d1, d2;
        float t = 0.22f;              // segment thickness (fatter = bolder)
        if(n<10){
            genDigitFilled(n, t, d1);
            verts.insert(verts.end(), d1.begin(), d1.end());
            out[n].width = 1.0f;
        }else{
            int tens=n/10, ones=n%10;
            genDigitFilled(tens, t, d1);
            genDigitFilled(ones, t, d2);
            float gap=0.20f, offset=0.70f+gap;
            // tens (left)
            for(size_t i=0;i<d1.size(); i+=2){ verts.push_back(d1[i]-offset); verts.push_back(d1[i+1]); }
            // ones (right)
            for(size_t i=0;i<d2.size(); i+=2){ verts.push_back(d2[i]+offset); verts.push_back(d2[i+1]); }
            out[n].width = 2.0f;
        }
        out[n].mesh = makeMesh(verts, GL_TRIANGLES);
    }
    return out;
}

// ---------- Time helper ----------
static double nowSeconds(){
    using namespace std::chrono;
    auto tp = system_clock::now();
    auto s  = time_point_cast<seconds>(tp);
    return (double)s.time_since_epoch().count()
         + duration<double>(tp - s).count();
}

int main(){
    // Window / context
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

    GLuint prog = makeProgram(kVS,kFS);
    GLint uAngle = glGetUniformLocation(prog,"uAngle");
    GLint uScale = glGetUniformLocation(prog,"uScale");
    GLint uTrans = glGetUniformLocation(prog,"uTranslate");
    GLint uColor = glGetUniformLocation(prog,"uColor");

    std::vector<float> tmp;
    // Bezel + dial + chapter ring
    genRing(256, 0.98f, 0.86f, tmp);      Mesh bezel  = makeMesh(tmp, GL_TRIANGLE_STRIP);
    genDiscFan(128, tmp);                 Mesh dial   = makeMesh(tmp, GL_TRIANGLE_FAN);
    genRing(256, 0.84f, 0.82f, tmp);      Mesh chap   = makeMesh(tmp, GL_TRIANGLE_STRIP);
    // Ticks
    genTicks(60, 0.82f, 0.88f, tmp);      Mesh minTicks = makeMesh(tmp, GL_LINES);
    genTicks(12, 0.78f, 0.90f, tmp);      Mesh hrTicks  = makeMesh(tmp, GL_LINES);
    // Hands
    genSpadeHour(tmp);                     Mesh hourHand = makeMesh(tmp);
    genPointerMinute(tmp);                 Mesh minHand  = makeMesh(tmp);
    genSecondThin(tmp);                    Mesh secHand  = makeMesh(tmp);
    genDiscFan(40, tmp);                   Mesh cap      = makeMesh(tmp, GL_TRIANGLE_FAN);
    // Numerals (filled)
    auto numerals = buildNumerals();

    glEnable(GL_MULTISAMPLE);
    glClearColor(1,1,1,1);

    const double TAU = 6.28318530718;

    auto placeAngle = [&](int n)->float {
        // n=12 at top; others clockwise every 30 degrees
        int idx = n % 12; // 12 -> 0
        return float(-TAU*(idx/12.0f) + TAU*0.25f);
    };

    while(!glfwWindowShouldClose(win)){
        glfwPollEvents();

        // time: ticking seconds, smooth minute/hour
        double tnow = nowSeconds();
        std::time_t tt = (std::time_t)tnow;
        std::tm lt{};
    #if defined(_WIN32)
        localtime_s(&lt,&tt);
    #else
        lt = *std::localtime(&tt);
    #endif
        int s_i = lt.tm_sec;         // tick exactly each second
        double s = double(s_i);
        double m = lt.tm_min + s/60.0;
        double h = (lt.tm_hour%12) + m/60.0;

        auto toA = [&](double f)->float { return float(-TAU*f + TAU*0.25f); };
        float aS = toA(s/60.0);
        float aM = toA(m/60.0);
        float aH = toA(h/12.0);

        int W,H; glfwGetFramebufferSize(win,&W,&H);
        glViewport(0,0,W,H);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(prog);
        glUniform1f(uAngle, 0.0f);
        glUniform2f(uTrans, 0,0);

        // bezel
        glUniform3f(uColor, 0.42f,0.22f,0.12f);
        glUniform2f(uScale, 1.0f,1.0f); bezel.draw();
        // white dial
        glUniform3f(uColor, 1,1,1);
        glUniform2f(uScale, 0.86f,0.86f); dial.draw();
        // chapter ring
        glUniform3f(uColor, 0.75f,0.75f,0.75f);
        glUniform2f(uScale, 1.0f,1.0f); chap.draw();

        // ticks
        glUniform3f(uColor, 0,0,0);
        glLineWidth(1.2f);  minTicks.draw(); // visual thin
        glLineWidth(2.0f);  hrTicks.draw();

        // numerals (filled, upright)
        float rNum = 0.66f;   // radius
        float sNum = 0.16f;   // overall size of digits
        glUniform3f(uColor, 0,0,0);
        for(int n=1;n<=12;n++){
            float ang = placeAngle(n);
            float cx = std::cos(ang)*rNum;
            float cy = std::sin(ang)*rNum;
            glUniform1f(uAngle, 0.0f);          // upright
            glUniform2f(uScale, sNum, sNum);
            glUniform2f(uTrans, cx, cy);
            numerals[n].mesh.draw();
        }

        // hands
        glUniform2f(uTrans, 0,0);
        glUniform3f(uColor, 0,0,0);
        glUniform1f(uAngle, aH); glUniform2f(uScale, 0.06f, 0.55f); hourHand.draw();
        glUniform1f(uAngle, aM); glUniform2f(uScale, 0.04f, 0.88f); minHand.draw();

        glUniform3f(uColor, 0.80f,0.70f,0.35f);         // gold second
        glUniform1f(uAngle, aS); glUniform2f(uScale, 0.02f, 0.92f); secHand.draw();

        glUniform3f(uColor, 0,0,0);
        glUniform1f(uAngle, 0.0f); glUniform2f(uScale, 0.035f, 0.035f); cap.draw();

        glfwSwapBuffers(win);
    }

    for(int n=1;n<=12;n++) numerals[n].mesh.destroy();
    cap.destroy(); secHand.destroy(); minHand.destroy(); hourHand.destroy();
    hrTicks.destroy(); minTicks.destroy(); chap.destroy(); dial.destroy(); bezel.destroy();
    glDeleteProgram(prog);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
