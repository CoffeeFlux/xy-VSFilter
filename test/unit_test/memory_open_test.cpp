// Checks that subtitles opened from memory (csri_open_mem, as used by Aegisub
// and for embedded subtitle streams) render exactly like the same script
// opened from a file, for each of the text encodings CTextFile detects.
//
// Scripts are read from the directory named by the XY_TEST_DATA environment
// variable (default: "test", relative to the working directory).

#include <windows.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include <vector>

#define CSRIAPI extern "C" __declspec(dllimport)
#include "csri.h"

namespace
{
    std::wstring DataDir()
    {
        wchar_t buf[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"XY_TEST_DATA", buf, MAX_PATH);
        return (n > 0 && n < MAX_PATH) ? std::wstring(buf) : std::wstring(L"test");
    }

    std::vector<std::wstring> ListScripts(const std::wstring& dir)
    {
        std::vector<std::wstring> files;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*.ass").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                files.push_back(dir + L"\\" + fd.cFileName);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    bool ReadAll(const std::wstring& path, std::vector<BYTE>& data)
    {
        FILE* f = NULL;
        if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) {
            return false;
        }
        BYTE buf[4096];
        size_t n;
        data.clear();
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            data.insert(data.end(), buf, buf + n);
        }
        fclose(f);
        return true;
    }

    bool WriteAll(const std::wstring& path, const std::vector<BYTE>& data)
    {
        FILE* f = NULL;
        if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) {
            return false;
        }
        bool ok = data.empty() || fwrite(&data[0], 1, data.size(), f) == data.size();
        fclose(f);
        return ok;
    }

    enum Variant { AS_IS, UTF8_BOM, UTF16LE_BOM, UTF16BE_BOM, VARIANT_COUNT };
    const char* const kVariantNames[] = { "as-is", "UTF-8 BOM", "UTF-16LE BOM", "UTF-16BE BOM" };

    // Decodes a script by its BOM (UTF-16LE/BE, UTF-8; no BOM is taken as UTF-8).
    std::wstring Decode(const std::vector<BYTE>& src)
    {
        std::wstring wide;
        if (src.size() >= 2 && (src[0] == 0xff && src[1] == 0xfe || src[0] == 0xfe && src[1] == 0xff)) {
            bool le = src[0] == 0xff;
            for (size_t i = 2; i + 1 < src.size(); i += 2) {
                wide += (wchar_t)(le ? src[i] | src[i + 1] << 8 : src[i] << 8 | src[i + 1]);
            }
            return wide;
        }
        size_t skip = (src.size() >= 3 && src[0] == 0xef && src[1] == 0xbb && src[2] == 0xbf) ? 3 : 0;
        if (src.size() > skip) {
            const char* p = (const char*)&src[skip];
            int len = (int)(src.size() - skip);
            int n = MultiByteToWideChar(CP_UTF8, 0, p, len, NULL, 0);
            wide.resize(n);
            MultiByteToWideChar(CP_UTF8, 0, p, len, &wide[0], n);
        }
        return wide;
    }

    // Re-encodes a script into the given variant.
    std::vector<BYTE> Encode(const std::vector<BYTE>& src, Variant v)
    {
        if (v == AS_IS) {
            return src;
        }

        std::wstring wide = Decode(src);
        std::vector<BYTE> out;
        if (v == UTF8_BOM) {
            out.push_back(0xef);
            out.push_back(0xbb);
            out.push_back(0xbf);
            if (!wide.empty()) {
                int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), NULL, 0, NULL, NULL);
                size_t at = out.size();
                out.resize(at + n);
                WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), (char*)&out[at], n, NULL, NULL);
            }
            return out;
        }

        out.push_back(v == UTF16LE_BOM ? 0xff : 0xfe);
        out.push_back(v == UTF16LE_BOM ? 0xfe : 0xff);
        for (size_t i = 0; i < wide.size(); i++) {
            BYTE lo = (BYTE)(wide[i] & 0xff), hi = (BYTE)(wide[i] >> 8);
            out.push_back(v == UTF16LE_BOM ? lo : hi);
            out.push_back(v == UTF16LE_BOM ? hi : lo);
        }
        return out;
    }

    std::string Narrow(const std::wstring& s)
    {
        int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, NULL, 0, NULL, NULL);
        std::string out(n > 0 ? n - 1 : 0, '\0');
        if (n > 1) {
            WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &out[0], n, NULL, NULL);
        }
        return out;
    }

    const int kWidth = 640;
    const int kHeight = 360;

    void RenderFrame(csri_inst* inst, double time, std::vector<BYTE>& buf)
    {
        buf.assign(kWidth * 4 * kHeight, 0);
        csri_frame frame;
        memset(&frame, 0, sizeof(frame));
        frame.pixfmt = CSRI_F_BGR_;
        frame.planes[0] = &buf[0];
        frame.strides[0] = kWidth * 4;
        csri_render(inst, &frame, time);
    }
}

TEST(MemoryOpen, RendersLikeFileOpen)
{
    std::vector<std::wstring> scripts = ListScripts(DataDir());
    ASSERT_FALSE(scripts.empty()) << "no .ass scripts found in " << Narrow(DataDir());

    csri_rend* rend = csri_renderer_default();
    ASSERT_TRUE(rend != NULL);

    wchar_t tempDir[MAX_PATH];
    ASSERT_NE(0u, GetTempPathW(MAX_PATH, tempDir));
    const std::wstring tempPath = std::wstring(tempDir) + L"xy_memory_open_test.ass";

    int opened = 0, frames = 0, nonEmptyFrames = 0;
    std::vector<BYTE> fileFrame, memFrame;

    for (size_t i = 0; i < scripts.size(); i++) {
        std::vector<BYTE> original;
        ASSERT_TRUE(ReadAll(scripts[i], original)) << Narrow(scripts[i]);

        for (int v = 0; v < VARIANT_COUNT; v++) {
            SCOPED_TRACE(Narrow(scripts[i]) + " (" + kVariantNames[v] + ")");

            std::vector<BYTE> data = Encode(original, (Variant)v);
            ASSERT_TRUE(WriteAll(tempPath, data));

            csri_inst* fromFile = csri_open_file(rend, Narrow(tempPath).c_str(), NULL);
            csri_inst* fromMem = csri_open_mem(rend, data.empty() ? NULL : &data[0], data.size(), NULL);
            EXPECT_TRUE(fromFile != NULL) << "does not open from a file";
            EXPECT_TRUE(fromMem != NULL) << "does not open from memory";

            if (fromFile && fromMem) {
                opened++;
                csri_fmt fmt = { CSRI_F_BGR_, kWidth, kHeight };
                ASSERT_EQ(0, csri_request_fmt(fromFile, &fmt));
                ASSERT_EQ(0, csri_request_fmt(fromMem, &fmt));

                for (double t = 0; t < 60; t += 2) {
                    RenderFrame(fromFile, t, fileFrame);
                    RenderFrame(fromMem, t, memFrame);
                    ASSERT_TRUE(fileFrame == memFrame) << "frame at " << t << "s differs";
                    frames++;
                    if (std::find_if(fileFrame.begin(), fileFrame.end(),
                                     [](BYTE b) { return b != 0; }) != fileFrame.end()) {
                        nonEmptyFrames++;
                    }
                }
            }

            csri_close(fromFile);
            csri_close(fromMem);
        }
    }

    DeleteFileW(tempPath.c_str());

    printf("%d of %d script variants opened; %d frames compared, %d with subtitles\n",
        opened, (int)scripts.size() * VARIANT_COUNT, frames, nonEmptyFrames);

    // Guard against both paths silently rendering nothing.
    EXPECT_GT(nonEmptyFrames, 0);
}
