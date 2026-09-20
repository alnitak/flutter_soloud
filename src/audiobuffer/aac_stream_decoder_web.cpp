#if defined(__EMSCRIPTEN__)

#include "aac_stream_decoder.h"
#include "../native_decoder/native_audio_decoder.h"
#include <emscripten.h>
#include <vector>
#include <deque>
#include <cstring>
#include <iostream>
#include <atomic>

namespace {

static const int kAacSampleRates[16] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
    16000, 12000, 11025, 8000, 7350, 0, 0, 0
};

// Standard AC-3 frame sizes in 16-bit words: [frmsizecod >> 1][fscod] (ATSC A/52 Table 5.18)
static const uint16_t kAc3FrameSizes[19][3] = {
    { 64,   69,   96 },
    { 80,   87,  120 },
    { 96,  104,  144 },
    { 112, 121,  168 },
    { 128, 139,  192 },
    { 160, 174,  240 },
    { 192, 208,  288 },
    { 224, 243,  336 },
    { 256, 278,  384 },
    { 320, 348,  480 },
    { 384, 417,  576 },
    { 448, 487,  672 },
    { 512, 557,  768 },
    { 640, 696,  960 },
    { 768, 835, 1152 },
    { 896, 975, 1344 },
    { 1024, 1114, 1536 },
    { 1152, 1253, 1728 },
    { 1280, 1393, 1920 }
};

static std::atomic<int> sNextDecoderId{1};

class WebAACImpl : public AACDecoderWrapper::Impl {
public:
  explicit WebAACImpl(DetectedType format)
      : mFormat(format), mDecoderId(sNextDecoderId++) {}

  ~WebAACImpl() override {
    EM_ASM({
      var id = $0;
      if (globalThis._soloudWebDecoders && globalThis._soloudWebDecoders[id]) {
        var d = globalThis._soloudWebDecoders[id];
        try {
          if (d.decoder && d.decoder.state !== 'closed') {
            d.decoder.close();
          }
        } catch (e) {}
        delete globalThis._soloudWebDecoders[id];
      }
    }, mDecoderId);
  }

  bool initialize(int sampleRate, int channels) override {
    mTargetSampleRate = sampleRate > 0 ? sampleRate : 44100;
    mTargetChannels = channels > 0 ? channels : 2;

    int formatCode = 0; // AAC
    if (mFormat == DetectedType::BUFFER_AC3) {
      formatCode = 1;
    } else if (mFormat == DetectedType::BUFFER_EAC3) {
      formatCode = 2;
    }

    int ok = EM_ASM_INT({
      var id = $0;
      var formatCode = $1;
      var sRate = $2;
      var ch = $3;

      if (typeof AudioDecoder === 'undefined') {
        console.warn("[WebCodecs] AudioDecoder is not supported in this browsing context.");
        return 0;
      }

      var codecStr = 'mp4a.40.2';
      if (formatCode === 1) codecStr = 'ac-3';
      else if (formatCode === 2) codecStr = 'ec-3';

      if (typeof AudioDecoder !== 'undefined' && typeof AudioDecoder.isConfigSupported === 'function') {
        if (!globalThis._soloudCodecSupport) {
          globalThis._soloudCodecSupport = {};
          AudioDecoder.isConfigSupported({ codec: 'ac-3', sampleRate: 48000, numberOfChannels: 2 })
            .then(function(r) { globalThis._soloudCodecSupport['ac-3'] = r.supported; })
            .catch(function() { globalThis._soloudCodecSupport['ac-3'] = false; });
          AudioDecoder.isConfigSupported({ codec: 'ec-3', sampleRate: 48000, numberOfChannels: 2 })
            .then(function(r) { globalThis._soloudCodecSupport['ec-3'] = r.supported; })
            .catch(function() { globalThis._soloudCodecSupport['ec-3'] = false; });
        }
      }

      if (formatCode === 1 || formatCode === 2) {
        var codecName = formatCode === 1 ? 'AC-3' : 'E-AC-3';
        console.warn("[flutter_soloud] " + codecName + " audio streaming and decoding is not supported on the Web platform. " +
                     "Web browsers (including Safari, Chrome, and Firefox) do not support raw " + codecName + " elementary bitstreams in WebCodecs or Web Audio. " +
                     "Please use AAC (ADTS), Opus, MP3, FLAC, WAV, or PCM on the Web.");
        return -1;
      }

      if (!globalThis._soloudWebDecoders) {
        globalThis._soloudWebDecoders = {};
      }

      var d = {};
      d.id = id;
      d.formatCode = formatCode;
      d.codecStr = codecStr;
      d.sampleRate = sRate;
      d.channels = ch;
      d.outputSampleRate = sRate;
      d.outputChannels = ch;
      d.configured = false;
      d.error = false;
      d.unsupported = false;
      d.queue = [];
      d.queueOffset = 0;
      d.totalSamples = 0;
      d.ptsUs = 0;

      try {
        var initOptions = ({
          output: function(frame) {
            var nFrames = frame.numberOfFrames;
            var nCh = frame.numberOfChannels;
            d.outputSampleRate = frame.sampleRate;
            d.outputChannels = nCh;
            var total = nFrames * nCh;
            var buf = new Float32Array(total);
            try {
              for (var c = 0; c < nCh; c++) {
                var p = new Float32Array(nFrames);
                frame.copyTo(p, ({ planeIndex: c }));
                for (var i = 0; i < nFrames; i++) {
                  buf[i * nCh + c] = p[i];
                }
              }
            } catch (e) {
              console.error("[WebCodecs] frame copyTo failed:", e);
            }
            frame.close();
            d.queue.push(buf);
            d.totalSamples += total;
          },
          error: function(err) {
            console.error("[WebCodecs] AudioDecoder error:", err);
            d.error = true;
            if (err && (
                err.name === 'OperationError' ||
                err.name === 'EncodingError' ||
                (err.message && (
                  err.message.indexOf('Unsupported configuration') !== -1 ||
                  err.message.indexOf('InternalAudioDecoderCocoa') !== -1
                ))
            )) {
              d.unsupported = true;
            }
          }
        });

        d.decoder = new AudioDecoder(initOptions);

        var configObj = ({
          codec: codecStr,
          sampleRate: sRate,
          numberOfChannels: ch
        });
        d.decoder.configure(configObj);
        d.configured = true;
        globalThis._soloudWebDecoders[id] = d;
        return 1;
      } catch (e) {
        console.warn("[WebCodecs] AudioDecoder initialization failed:", e);
        return 0;
      }
    }, mDecoderId, formatCode, mTargetSampleRate, mTargetChannels);

    if (ok == -1) {
      mUnsupportedFormat = true;
      mInitialized = false;
      return false;
    }

    mInitialized = (ok == 1);
    return mInitialized;
  }

  std::pair<std::vector<float>, DecoderError> decode(
      std::vector<unsigned char> &buffer, int *samplerate, int *channels,
      size_t maxOutputSamples) override {
    if (mUnsupportedFormat) {
      return {{}, DecoderError::FormatNotSupported};
    }

    if (!mInitialized) {
      if (!initialize(samplerate ? *samplerate : 44100, channels ? *channels : 2)) {
        if (mUnsupportedFormat) {
          return {{}, DecoderError::FormatNotSupported};
        }
        return {{}, DecoderError::FailedToCreateDecoder};
      }
    }

    if (!buffer.empty()) {
      if (mFormat == DetectedType::BUFFER_AAC) {
        feedAacFrames(buffer);
      } else {
        feedAc3Frames(buffer);
      }
    }

    std::vector<float> decodedData;
    int availableFloats = EM_ASM_INT({
      var d = globalThis._soloudWebDecoders ? globalThis._soloudWebDecoders[$0] : null;
      return d ? d.totalSamples : 0;
    }, mDecoderId);

    if (availableFloats > 0) {
      size_t toRead = static_cast<size_t>(availableFloats);
      decodedData.resize(toRead);
      int copied = EM_ASM_INT({
        var id = $0;
        var pOut = $1;
        var maxFloats = $2;
        var d = globalThis._soloudWebDecoders ? globalThis._soloudWebDecoders[id] : null;
        if (!d || d.queue.length === 0) return 0;

        var copied = 0;
        while (d.queue.length > 0 && copied < maxFloats) {
          var cur = d.queue[0];
          var avail = cur.length - d.queueOffset;
          var count = Math.min(avail, maxFloats - copied);

          Module_soloud.HEAPF32.set(
            cur.subarray(d.queueOffset, d.queueOffset + count),
            (pOut >> 2) + copied
          );

          copied += count;
          d.queueOffset += count;
          d.totalSamples -= count;
          if (d.queueOffset >= cur.length) {
            d.queue.shift();
            d.queueOffset = 0;
          }
        }
        return copied;
      }, mDecoderId, (uintptr_t)decodedData.data(), static_cast<int>(toRead));

      decodedData.resize(static_cast<size_t>(copied));
    }

    if (samplerate) {
      int sr = EM_ASM_INT({
        var d = globalThis._soloudWebDecoders ? globalThis._soloudWebDecoders[$0] : null;
        return d ? d.outputSampleRate : 0;
      }, mDecoderId);
      if (sr > 0) *samplerate = sr;
    }
    if (channels) {
      int ch = EM_ASM_INT({
        var d = globalThis._soloudWebDecoders ? globalThis._soloudWebDecoders[$0] : null;
        return d ? d.outputChannels : 0;
      }, mDecoderId);
      if (ch > 0) *channels = ch;
    }

    int isUnsupported = EM_ASM_INT({
      var d = globalThis._soloudWebDecoders ? globalThis._soloudWebDecoders[$0] : null;
      return (d && d.unsupported) ? 1 : 0;
    }, mDecoderId);

    if (isUnsupported) {
      return {std::move(decodedData), DecoderError::FormatNotSupported};
    }

    int hasError = EM_ASM_INT({
      var d = globalThis._soloudWebDecoders ? globalThis._soloudWebDecoders[$0] : null;
      return (d && d.error) ? 1 : 0;
    }, mDecoderId);

    if (hasError) {
      return {std::move(decodedData), DecoderError::FailedToCreateDecoder};
    }

    return {std::move(decodedData), DecoderError::NoError};
  }

  void setDataEnded() override {
    mDataEnded = true;
    EM_ASM({
      var d = globalThis._soloudWebDecoders ? globalThis._soloudWebDecoders[$0] : null;
      if (d && d.decoder && d.decoder.state === 'configured') {
        try {
          d.decoder.flush();
        } catch (e) {}
      }
    }, mDecoderId);
  }

  bool hasPendingData() const override {
    return EM_ASM_INT({
      var d = globalThis._soloudWebDecoders ? globalThis._soloudWebDecoders[$0] : null;
      if (!d) return 0;
      var queueSize = (d.decoder && typeof d.decoder.decodeQueueSize === 'number')
          ? d.decoder.decodeQueueSize : 0;
      return (d.totalSamples > 0 || queueSize > 0) ? 1 : 0;
    }, mDecoderId) != 0;
  }

private:
  void feedAacFrames(std::vector<unsigned char> &buffer) {
    size_t offset = 0;
    const size_t available = buffer.size();

    while (offset + 7 <= available) {
      if (buffer[offset] != 0xFF || (buffer[offset + 1] & 0xF6) != 0xF0) {
        offset++;
        continue;
      }

      size_t frameLen = ((buffer[offset + 3] & 0x03) << 11) |
                        (buffer[offset + 4] << 3) |
                        ((buffer[offset + 5] & 0xE0) >> 5);

      if (frameLen < 7 || frameLen > 8192) {
        offset++;
        continue;
      }

      if (offset + frameLen > available) {
        break;
      }

      int srIdx = (buffer[offset + 2] & 0x3C) >> 2;
      int chCfg = ((buffer[offset + 2] & 0x01) << 2) | ((buffer[offset + 3] & 0xC0) >> 6);
      int protectionAbsent = buffer[offset + 1] & 0x01;
      size_t headerSize = protectionAbsent ? 7 : 9;

      if (!mConfiguredStream && srIdx < 13 && chCfg >= 1 && chCfg <= 7) {
        int streamSampleRate = kAacSampleRates[srIdx];
        int streamChannels = chCfg;

        uint8_t desc[2];
        desc[0] = static_cast<uint8_t>((2 << 3) | (srIdx >> 1));
        desc[1] = static_cast<uint8_t>(((srIdx & 1) << 7) | (chCfg << 3));

        EM_ASM({
          var id = $0;
          var sRate = $1;
          var ch = $2;
          var pDesc = $3;
          var d = globalThis._soloudWebDecoders ? globalThis._soloudWebDecoders[id] : null;
          if (d && d.decoder) {
            try {
              var descBytes = new Uint8Array(2);
              descBytes.set(Module_soloud.HEAPU8.subarray(pDesc, pDesc + 2));
              var reconfigObj = ({
                codec: 'mp4a.40.2',
                sampleRate: sRate,
                numberOfChannels: ch,
                description: descBytes.buffer
              });
              d.decoder.configure(reconfigObj);
              d.sampleRate = sRate;
              d.channels = ch;
              d.outputSampleRate = sRate;
              d.outputChannels = ch;
            } catch (e) {
              console.warn("[WebCodecs] AAC reconfigure with description failed:", e);
            }
          }
        }, mDecoderId, streamSampleRate, streamChannels, (uintptr_t)desc);

        mConfiguredStream = true;
      }

      const uint8_t *framePtr = buffer.data() + offset;
      size_t sendLen = frameLen;
      if (mConfiguredStream && frameLen > headerSize) {
        framePtr += headerSize;
        sendLen -= headerSize;
      }

      EM_ASM({
        var id = $0;
        var pData = $1;
        var len = $2;
        var d = globalThis._soloudWebDecoders ? globalThis._soloudWebDecoders[id] : null;
        if (d && d.decoder && !d.error && d.decoder.state === 'configured') {
          try {
            var u8 = new Uint8Array(len);
            u8.set(Module_soloud.HEAPU8.subarray(pData, pData + len));
            var chunkObj = ({
              type: 'key',
              timestamp: d.ptsUs,
              data: u8
            });
            d.decoder.decode(new EncodedAudioChunk(chunkObj));
            var deltaUs = (1024 * 1000000) / (d.sampleRate || 44100);
            d.ptsUs += deltaUs;
          } catch (e) {
            console.error("[WebCodecs] decode AAC chunk failed:", e);
            d.error = true;
          }
        }
      }, mDecoderId, (uintptr_t)framePtr, static_cast<int>(sendLen));

      offset += frameLen;
    }

    if (offset > 0) {
      buffer.erase(buffer.begin(), buffer.begin() + offset);
    }
  }

  void feedAc3Frames(std::vector<unsigned char> &buffer) {
    size_t offset = 0;
    const size_t available = buffer.size();

    while (offset + 7 <= available) {
      int sync = NativeAudioDecoder::findAc3Syncword(buffer.data() + offset, available - offset);
      if (sync < 0) {
        if (available > 4) {
          offset = available - 4;
        }
        break;
      }
      offset += sync;
      const uint8_t *frameData = buffer.data() + offset;
      size_t currentAvail = available - offset;
      if (currentAvail < 7) break;

      size_t frameLen = 0;
      bool isEac3 = NativeAudioDecoder::isEac3(frameData, currentAvail);

      if (isEac3) {
        size_t frmsiz = ((frameData[2] & 0x07) << 8) | frameData[3];
        frameLen = (frmsiz + 1) * 2;
      } else {
        uint8_t fscod = frameData[4] >> 6;
        uint8_t frmsizecod = frameData[4] & 0x3F;
        if (fscod < 3 && (frmsizecod >> 1) < 19) {
          frameLen = kAc3FrameSizes[frmsizecod >> 1][fscod] * 2;
        }
      }

      if (frameLen == 0 || frameLen > 4096) {
        offset += 2;
        continue;
      }

      if (currentAvail < frameLen) {
        break;
      }

      EM_ASM({
        var id = $0;
        var pData = $1;
        var len = $2;
        var d = globalThis._soloudWebDecoders ? globalThis._soloudWebDecoders[id] : null;
        if (d && d.decoder && !d.error && d.decoder.state === 'configured') {
          try {
            var u8 = new Uint8Array(len);
            u8.set(Module_soloud.HEAPU8.subarray(pData, pData + len));
            var chunkObj = ({
              type: 'key',
              timestamp: d.ptsUs,
              data: u8
            });
            d.decoder.decode(new EncodedAudioChunk(chunkObj));
            var deltaUs = (1536 * 1000000) / (d.sampleRate || 48000);
            d.ptsUs += deltaUs;
          } catch (e) {
            console.error("[WebCodecs] decode AC-3 chunk failed:", e);
            d.error = true;
          }
        }
      }, mDecoderId, (uintptr_t)frameData, static_cast<int>(frameLen));

      offset += frameLen;
    }

    if (offset > 0) {
      buffer.erase(buffer.begin(), buffer.begin() + offset);
    }
  }

  DetectedType mFormat = DetectedType::BUFFER_AAC;
  int mDecoderId = 0;
  bool mInitialized = false;
  bool mUnsupportedFormat = false;
  bool mConfiguredStream = false;
  bool mDataEnded = false;
  int mTargetSampleRate = 44100;
  int mTargetChannels = 2;
};

} // namespace

std::unique_ptr<AACDecoderWrapper::Impl> createWebAACDecoderImpl(DetectedType format) {
  return std::make_unique<WebAACImpl>(format);
}

#endif // defined(__EMSCRIPTEN__)
