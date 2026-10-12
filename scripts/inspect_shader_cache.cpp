// Offline disassembly of the user's local shader cache; never runs guest code.
#define XXH_INLINE_ALL
#include <xxhash.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>

int main(int argc,char** argv) {try {
    if(argc!=3)throw std::runtime_error("Expected input .xsh and output directory");
    std::ifstream input(std::filesystem::u8path(argv[1]),std::ios::binary);
    if(!input)throw std::runtime_error("Cannot open cache; close GH2 normally before inspecting it");
    uint32_t magic=0,version=0;input.read(reinterpret_cast<char*>(&magic),4);input.read(reinterpret_cast<char*>(&version),4);
    if(!input || magic!=0x48534558 || version!=0x19122020)throw std::runtime_error("Unsupported XESH cache revision");
    const auto directory=std::filesystem::u8path(argv[2]);std::filesystem::create_directories(directory);
    size_t count=0,vertex=0;
    for(;;) {
        uint64_t hash=0;uint32_t packed=0;
        input.read(reinterpret_cast<char*>(&hash),8);
        if(input.eof()&&input.gcount()==0)break;
        input.read(reinterpret_cast<char*>(&packed),4);
        const uint32_t words=packed&0x7fffffff;
        if(!input || !words || words>65535)throw std::runtime_error("Truncated or invalid shader cache entry");
        std::vector<uint32_t> code(words);input.read(reinterpret_cast<char*>(code.data()),words*4);
        if(!input || XXH3_64bits(code.data(),words*4)!=hash)throw std::runtime_error("Truncated shader or hash mismatch");
        const bool pixel=packed>>31;
        rex::graphics::Shader shader(pixel?rex::graphics::xenos::ShaderType::kPixel:rex::graphics::xenos::ShaderType::kVertex,hash,code.data(),code.size());
        rex::string::StringBuffer buffer;shader.AnalyzeUcode(buffer);
        std::ostringstream name;name<<std::hex<<std::setfill('0')<<std::setw(16)<<hash<<(pixel?".ps.txt":".vs.txt");
        std::ofstream output(directory/name.str(),std::ios::binary);output<<shader.ucode_disassembly();
        if(!output)throw std::runtime_error("Shader disassembly write failed");
        ++count;vertex+=!pixel;
    }
    std::cout<<"Disassembled "<<count<<" cached shaders ("<<vertex<<" vertex). No game launched.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
