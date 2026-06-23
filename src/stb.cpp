#include "stb_image.h"
#include "stb_image_write.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
static std::string g_error;
extern "C" unsigned char *stbi_load(char const *filename, int *x, int *y, int *comp, int req_comp) {
    std::ifstream f(filename, std::ios::binary); std::string magic; f >> magic; if (magic != "P6") { g_error = "minimal facade reads binary PPM only; replace with upstream stb for PNG"; return nullptr; }
    f >> *x >> *y; int maxv; f >> maxv; f.get(); int c = req_comp ? req_comp : 3; *comp = 3; auto* p=(unsigned char*)std::malloc((*x)*(*y)*c); for(int i=0;i<(*x)*(*y);++i){ unsigned char rgb[3]; f.read((char*)rgb,3); for(int k=0;k<c;++k) p[i*c+k]=rgb[k%3]; } return p;
}
extern "C" void stbi_image_free(void *p){ std::free(p); }
extern "C" char const *stbi_failure_reason(void){ return g_error.c_str(); }
extern "C" int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride) { std::ofstream f(filename, std::ios::binary); if(!f) return 0; f << "P6\n" << w << " " << h << "\n255\n"; auto* p=(const unsigned char*)data; for(int y=0;y<h;++y){ auto* row=p+y*stride; for(int x=0;x<w;++x){ unsigned char rgb[3]={row[x*comp],row[x*comp+1],row[x*comp+2]}; f.write((char*)rgb,3); }} return 1; }
