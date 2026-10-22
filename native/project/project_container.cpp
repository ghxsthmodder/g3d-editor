#include "project/project_container.hpp"

#include <algorithm>
#include <cstring>

namespace g3d {
namespace {

void putUint32(uint8_t *at, uint32_t value) {
    at[0] = static_cast<uint8_t>(value & 0xffu);
    at[1] = static_cast<uint8_t>((value >> 8) & 0xffu);
    at[2] = static_cast<uint8_t>((value >> 16) & 0xffu);
    at[3] = static_cast<uint8_t>((value >> 24) & 0xffu);
}

uint32_t takeUint32(const uint8_t *at) {
    return static_cast<uint32_t>(at[0]) | (static_cast<uint32_t>(at[1]) << 8) |
           (static_cast<uint32_t>(at[2]) << 16) | (static_cast<uint32_t>(at[3]) << 24);
}

size_t padTo4(size_t length) {
    return (length + 3u) & ~static_cast<size_t>(3u);
}

}  // namespace

const char *projectMagic() {
    return "G3DP";
}

bool writeProjectContainer(const std::string &json, const std::vector<uint8_t> &bin,
                           std::vector<uint8_t> &out, std::string &error) {
    if (json.empty() || json.front() != '{') {
        error = "a descricao do projeto nao comeca com '{'";
        return false;
    }
    const size_t jsonBytes = padTo4(json.size());
    const size_t binBytes = padTo4(bin.size());
    if (jsonBytes > 0xffffffffu || binBytes > 0xffffffffu) {
        error = "o projeto e grande demais para um unico arquivo";
        return false;
    }

    out.assign(kProjectHeaderBytes + jsonBytes + binBytes, 0u);
    std::memcpy(out.data(), projectMagic(), 4);
    putUint32(out.data() + 4, kProjectVersion);
    putUint32(out.data() + 8, static_cast<uint32_t>(jsonBytes));
    putUint32(out.data() + 12, static_cast<uint32_t>(binBytes));
    // Spaces pad the description, zeros pad the data: the same rule glTF-binary follows, so the
    // bytes that are not part of either chunk say which of the two they belong to.
    std::fill(out.begin() + kProjectHeaderBytes,
              out.begin() + kProjectHeaderBytes + jsonBytes, ' ');
    std::copy(json.begin(), json.end(), out.begin() + kProjectHeaderBytes);
    if (!bin.empty()) {
        std::copy(bin.begin(), bin.end(), out.begin() + kProjectHeaderBytes + jsonBytes);
    }
    return true;
}

bool readProjectContainer(const void *data, size_t length, ProjectChunks &out, std::string &error) {
    out = ProjectChunks();
    if (data == nullptr || length < kProjectHeaderBytes) {
        error = "o arquivo e pequeno demais para ser um projeto";
        return false;
    }
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    if (std::memcmp(bytes, projectMagic(), 4) != 0) {
        error = "o arquivo nao comeca com G3DP: nao e um projeto deste editor";
        return false;
    }
    out.version = takeUint32(bytes + 4);
    if (out.version == 0 || out.version > kProjectVersion) {
        error = "versao de projeto " + std::to_string(out.version) +
                " nao existe neste editor (a mais nova e " + std::to_string(kProjectVersion) + ")";
        return false;
    }
    const size_t jsonBytes = takeUint32(bytes + 8);
    const size_t binBytes = takeUint32(bytes + 12);
    if (jsonBytes % 4u != 0u || binBytes % 4u != 0u) {
        error = "os blocos do projeto nao estao alinhados a 4 bytes";
        return false;
    }
    if (kProjectHeaderBytes + jsonBytes + binBytes > length) {
        error = "o arquivo termina antes do que os blocos anunciam";
        return false;
    }
    if (jsonBytes == 0u) {
        error = "o projeto nao tem descricao";
        return false;
    }
    out.json.assign(reinterpret_cast<const char *>(bytes + kProjectHeaderBytes), jsonBytes);
    if (binBytes != 0u) {
        out.bin.assign(bytes + kProjectHeaderBytes + jsonBytes,
                       bytes + kProjectHeaderBytes + jsonBytes + binBytes);
    }
    const size_t first = out.json.find_first_not_of(' ');
    if (first == std::string::npos || out.json[first] != '{') {
        error = "a descricao do projeto nao e um objeto JSON";
        return false;
    }
    out.json.resize(out.json.find_last_not_of(' ') + 1u);
    if (out.json.back() != '}') {
        error = "a descricao do projeto nao termina com '}'";
        return false;
    }
    return true;
}

}  // namespace g3d
