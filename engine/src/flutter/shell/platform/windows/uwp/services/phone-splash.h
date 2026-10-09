#pragma once
#include <vector>

// SDK-generated FSP1 asset: little-endian width/height, explicit background RGBA,
// followed by premultiplied RGBA pixels. Dimensions are package metadata.
struct PhoneSplashImage {
    UINT32 width=0,height=0;
    unsigned char background[4]{};
    std::vector<unsigned char> pixels;
    HRESULT Load(const wchar_t* asset) {
        HANDLE file=CreateFile2(asset,GENERIC_READ,FILE_SHARE_READ,OPEN_EXISTING,nullptr);
        if(file==INVALID_HANDLE_VALUE)return HRESULT_FROM_WIN32(GetLastError());
        struct CloseFile {HANDLE handle;~CloseFile(){CloseHandle(handle);}} closer{file};
        UINT32 header[4]{};DWORD count=0;LARGE_INTEGER length{};
        if(!ReadFile(file,header,sizeof(header),&count,nullptr)||count!=sizeof(header)||!GetFileSizeEx(file,&length))return E_FAIL;
        const uint64_t pixelCount=uint64_t(header[1])*header[2];
        if(pixelCount>MAXDWORD/4)return E_INVALIDARG;
        const uint64_t size=pixelCount*4;
        if(header[0]!=0x31505346 || !header[1] || !header[2] || size>MAXDWORD || length.QuadPart!=static_cast<int64_t>(sizeof(header)+size))return E_INVALIDARG;
        try {pixels.resize(static_cast<size_t>(size));} catch(const std::bad_alloc&) {return E_OUTOFMEMORY;}
        if(!ReadFile(file,pixels.data(),static_cast<DWORD>(size),&count,nullptr)||count!=size)return E_FAIL;
        width=header[1];height=header[2];memcpy(background,&header[3],4);return S_OK;
    }
};

// Draw the packaged manifest splash image on the hardware ANGLE context.
static HRESULT PhoneDrawSplash(HMODULE gles, const PhoneSplashImage& imageData,
                               const Rect& bounds, const Rect& image,
                               GLsizei width, GLsizei height) {
#define SPLASH_GL(type, name) auto name=Proc<type>(gles,#name);if(!name)return E_NOINTERFACE
    SPLASH_GL(PFNGLCREATESHADERPROC,glCreateShader);
    SPLASH_GL(PFNGLSHADERSOURCEPROC,glShaderSource);
    SPLASH_GL(PFNGLCOMPILESHADERPROC,glCompileShader);
    SPLASH_GL(PFNGLGETSHADERIVPROC,glGetShaderiv);
    SPLASH_GL(PFNGLDELETESHADERPROC,glDeleteShader);
    SPLASH_GL(PFNGLCREATEPROGRAMPROC,glCreateProgram);
    SPLASH_GL(PFNGLATTACHSHADERPROC,glAttachShader);
    SPLASH_GL(PFNGLBINDATTRIBLOCATIONPROC,glBindAttribLocation);
    SPLASH_GL(PFNGLLINKPROGRAMPROC,glLinkProgram);
    SPLASH_GL(PFNGLGETPROGRAMIVPROC,glGetProgramiv);
    SPLASH_GL(PFNGLUSEPROGRAMPROC,glUseProgram);
    SPLASH_GL(PFNGLDELETEPROGRAMPROC,glDeleteProgram);
    SPLASH_GL(PFNGLGENTEXTURESPROC,glGenTextures);
    SPLASH_GL(PFNGLBINDTEXTUREPROC,glBindTexture);
    SPLASH_GL(PFNGLTEXPARAMETERIPROC,glTexParameteri);
    SPLASH_GL(PFNGLTEXIMAGE2DPROC,glTexImage2D);
    SPLASH_GL(PFNGLDELETETEXTURESPROC,glDeleteTextures);
    SPLASH_GL(PFNGLVIEWPORTPROC,glViewport);
    SPLASH_GL(PFNGLCLEARCOLORPROC,glClearColor);
    SPLASH_GL(PFNGLCLEARPROC,glClear);
    SPLASH_GL(PFNGLENABLEPROC,glEnable);
    SPLASH_GL(PFNGLDISABLEPROC,glDisable);
    SPLASH_GL(PFNGLBLENDFUNCPROC,glBlendFunc);
    SPLASH_GL(PFNGLVERTEXATTRIBPOINTERPROC,glVertexAttribPointer);
    SPLASH_GL(PFNGLENABLEVERTEXATTRIBARRAYPROC,glEnableVertexAttribArray);
    SPLASH_GL(PFNGLDISABLEVERTEXATTRIBARRAYPROC,glDisableVertexAttribArray);
    SPLASH_GL(PFNGLDRAWARRAYSPROC,glDrawArrays);
    SPLASH_GL(PFNGLFINISHPROC,glFinish);
    SPLASH_GL(PFNGLGETERRORPROC,glGetError);
#undef SPLASH_GL
    const char* vertex="attribute vec2 p; attribute vec2 t; varying vec2 uv; void main(){uv=t;gl_Position=vec4(p,0.,1.);}";
    const char* fragment="precision mediump float; varying vec2 uv; uniform sampler2D image; void main(){gl_FragColor=texture2D(image,uv);}";
    GLuint vs=glCreateShader(GL_VERTEX_SHADER),fs=glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs,1,&vertex,nullptr);glCompileShader(vs);
    glShaderSource(fs,1,&fragment,nullptr);glCompileShader(fs);
    GLint vOk=0,fOk=0;glGetShaderiv(vs,GL_COMPILE_STATUS,&vOk);glGetShaderiv(fs,GL_COMPILE_STATUS,&fOk);
    if(!vOk || !fOk){glDeleteShader(vs);glDeleteShader(fs);return E_FAIL;}
    GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);
    glBindAttribLocation(program,0,"p");glBindAttribLocation(program,1,"t");glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
    glDeleteShader(vs);glDeleteShader(fs);
    if(!linked){glDeleteProgram(program);return E_FAIL;}
    GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,static_cast<GLsizei>(imageData.width),static_cast<GLsizei>(imageData.height),0,GL_RGBA,GL_UNSIGNED_BYTE,imageData.pixels.data());
    float left=2*(image.X-bounds.X)/bounds.Width-1,right=left+2*image.Width/bounds.Width;
    float top=1-2*(image.Y-bounds.Y)/bounds.Height,bottom=top-2*image.Height/bounds.Height;
    const GLfloat quad[]={left,top,0,0,left,bottom,0,1,right,top,1,0,right,bottom,1,1};
    glViewport(0,0,width,height);glClearColor(imageData.background[0]/255.f,imageData.background[1]/255.f,imageData.background[2]/255.f,imageData.background[3]/255.f);glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program);glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(GLfloat),quad);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(GLfloat),quad+2);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glDrawArrays(GL_TRIANGLE_STRIP,0,4);glFinish();
    GLenum error=glGetError();
    glDisableVertexAttribArray(0);glDisableVertexAttribArray(1);glDisable(GL_BLEND);
    glUseProgram(0);glBindTexture(GL_TEXTURE_2D,0);glDeleteTextures(1,&texture);glDeleteProgram(program);
    return error==GL_NO_ERROR?S_OK:E_FAIL;
}
