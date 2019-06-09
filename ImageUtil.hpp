#ifndef ImageUtil_HPP
#define ImageUtil_HPP

#include <cmath>
#include <cstdio>

namespace ImageUtil {
  enum IMG_TYPE {
                 IMG_PNG,
                 IMG_JPG,
                 IMG_BMP,
                 IMG_GIF,
                 IMG_TGA,
                 IMG_UNKNOWN,
  };

  IMG_TYPE checkHeader(FILE* fp)
  {
    unsigned char header[17] = {0};

    fread(header, 1, sizeof(header), fp);
    fseek(fp, 0L, SEEK_SET);

    if(header[0] == 0x89 && header[1] == 0x50 &&
       header[2] == 0x4E && header[3] == 0x47 &&
       header[4] == 0x0D && header[5] == 0x0A &&
       header[6] == 0x1A && header[7] == 0x0A)
      return IMG_TYPE::IMG_PNG;

    if(header[0] == 0xFF && header[1] == 0xD8)
      return IMG_TYPE::IMG_JPG;

    if(header[0] == 0x42 && header[1] == 0x4D && header[6] == 0x00 &&
       header[7] == 0x00 && header[8] == 0x00 && header[9] == 0x00 &&
       (header[14] == 0x28 || header[14] == 0x0C || header[14] == 40 ||
        header[14] == 108 || header[14] == 124))
      return IMG_TYPE::IMG_BMP;

    if(header[0] == 0x47 && header[1] == 0x49 && header[2] == 0x46) {
      for(int i = 3; i < 15; ++i)
        if(header[i] < 0x08 && header[i] != 0x00) return IMG_TYPE::IMG_GIF;
    }

    // IMG_TGA
    union {
      unsigned char c[4];
      int i;
    } ci;
    ci.c[0] = ci.c[1] = ci.c[2] = ci.c[3] = 0;
    if(header[1] == 0x01) {
      if(header[2] == 0x01 || header[2] == 0x09) {
        ci.c[0] = header[12], ci.c[1] = header[13];
        if(ci.i >= 1) {
          ci.c[0] = header[14], ci.c[1] = header[15];
          if(ci.i >= 1) {
            if(header[16] == 8 || header[16] == 16)
              return IMG_TYPE::IMG_TGA;
          }
        }
      }
    }
    else if(header[1] == 0x00) {
      if(header[2] == 0x02 || header[2] == 0x03 || header[2] == 0x0A || header[2] == 0x0B) {
        ci.c[0] = header[12], ci.c[1] = header[13];
        if(ci.i >= 1) {
          ci.c[0] = header[14], ci.c[1] = header[15];
          if(ci.i >= 1) {
            if(header[16] == 8 || header[16] == 15 || header[16] == 16 ||
               header[16] == 24 || header[16] == 32) {
              return IMG_TYPE::IMG_TGA;
            }
          }
        }
      }
    }

    return IMG_TYPE::IMG_UNKNOWN;
  }

  bool getSize(FILE* fp, IMG_TYPE type, int& width, int& height)
  {
    unsigned char buf[64] = {0};
    union {
      unsigned char c[4];
      int i;
    } ci;

    ci.c[0] = ci.c[1] = ci.c[2] = ci.c[3] = 0;
    width = -1, height = -1;

    switch(type) {
    case IMG_TYPE::IMG_PNG:
      fread(buf, 1, 33, fp);

      ci.c[3] = buf[0x08 + 0x08], ci.c[2] = buf[0x08 + 0x09];
      ci.c[1] = buf[0x08 + 0x0A], ci.c[0] = buf[0x08 + 0x0B];
      width = ci.i;

      ci.c[3] = buf[0x08 + 0x0C], ci.c[2] = buf[0x08 + 0x0D];
      ci.c[1] = buf[0x08 + 0x0E], ci.c[0] = buf[0x08 + 0x0F];
      height = ci.i;
      break;

    case IMG_TYPE::IMG_JPG:
      fseek(fp, 4, SEEK_SET);
      while(!(buf[0] == 0xFF && (buf[1] == 0xC0 ||
                                 buf[1] == 0xC1 ||
                                 buf[1] == 0xC2 ||
                                 buf[1] == 0xC3))) {
        fseek(fp, ci.i - 2, SEEK_CUR);
        fread(buf, 1, 4, fp);
        ci.c[1] = buf[2], ci.c[0] = buf[3];

        if(feof(fp)) break;
      }
      if(!feof(fp)) {
        fseek(fp, 1, SEEK_CUR);
        fread(buf, 1, 4, fp);

        ci.c[1] = buf[0], ci.c[0] = buf[1];
        height = ci.i;

        ci.c[1] = buf[2], ci.c[0] = buf[3];
        width = ci.i;
      }
      break;

    case IMG_TYPE::IMG_BMP:
      fseek(fp, 14, SEEK_SET);
      fread(buf, 1, 4, fp);
      ci.c[3] = buf[3], ci.c[2] = buf[2];
      ci.c[1] = buf[1], ci.c[0] = buf[0];

      // Windows
      if(ci.i == 40 || ci.i == 108 || ci.i == 124) {
        fread(buf, 1, 8, fp);
        ci.c[3] = buf[3], ci.c[2] = buf[2];
        ci.c[1] = buf[1], ci.c[0] = buf[0];
        width = ci.i;

        ci.c[3] = buf[7], ci.c[2] = buf[6];
        ci.c[1] = buf[5], ci.c[0] = buf[4];
        height = ci.i;
      }
      // OS/2
      else if(ci.i == 12) {
        fread(buf, 1, 4, fp);
        ci.c[1] = buf[1], ci.c[0] = buf[0];
        width = ci.i;

        ci.c[1] = buf[3], ci.c[0] = buf[2];
        height = ci.i;
      }
      break;

    case IMG_TYPE::IMG_GIF:
      fseek(fp, 6, SEEK_SET);
      fread(buf, 1, 4, fp);

      ci.c[1] = buf[1], ci.c[0] = buf[0];
      width = ci.i;

      ci.c[1] = buf[3], ci.c[0] = buf[2];
      height = ci.i;
      break;

    case IMG_TYPE::IMG_TGA:
      fseek(fp, 12, SEEK_SET);
      fread(buf, 1, 4, fp);
      ci.c[0] = buf[0], ci.c[1] = buf[1];
      width = ci.i;
      ci.c[0] = buf[2], ci.c[1] = buf[3];
      height = ci.i;
      break;

    case IMG_TYPE::IMG_UNKNOWN:
    default:
      return false;
    };

    return true;
  }

  void CalcScaleSize_KeepAspectRatio(int srcW, int srcH,
                                     int scaleW, int scaleH,
                                     int& dstW, int& dstH) {
    double w_ratio = static_cast<double>(scaleW) / static_cast<double>(srcW);
    double h_ratio = static_cast<double>(scaleH) / static_cast<double>(srcH);
    double ratio = 0.0;

    if(w_ratio > h_ratio) ratio = h_ratio;
    else ratio = w_ratio;

    dstW = static_cast<int>(floor(srcW * ratio));
    dstH = static_cast<int>(floor(srcH * ratio));
  }
};

#endif
