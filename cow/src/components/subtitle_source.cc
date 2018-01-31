/**
 * Copyright (C) 2017 Alibaba Group Holding Limited. All Rights Reserved.
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unistd.h>
#include "multimedia/media_buffer.h"
#include "multimedia/media_attr_str.h"
#include "subtitle_source.h"
#include "multimedia/component.h"
#include <fstream>

namespace YUNOS_MM
{

DEFINE_LOGTAG(SubtitleSource)
DEFINE_LOGTAG(SubtitleSource::SubtitleSourceReader)
DEFINE_LOGTAG(SubtitleSource::ReaderThread)
#ifdef VERBOSE
#undef VERBOSE
#endif
#define VERBOSE WARNING
#define ENTER() VERBOSE(">>>\n")
#define FLEAVE() do {VERBOSE(" <<<\n"); return;}while(0)
#define FLEAVE_WITH_CODE(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

static const char * COMPONENT_NAME = "SubtitleSource";
static const char * MMTHREAD_NAME = "SubtitleSource::ReaderThread";

#define TrafficControlLowBar     1
#define TrafficControlHighBar    50
#define ENTER() VERBOSE(">>>\n")
#define EXIT() do {VERBOSE(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN(_code) do {VERBOSE("<<<(status: %d)\n", (_code)); return (_code);}while(0)

#define ENTER1() DEBUG(">>>\n")
#define EXIT1() do {DEBUG(" <<<\n"); return;}while(0)
#define EXIT_AND_RETURN1(_code) do {DEBUG("<<<(status: %d)\n", (_code)); return (_code);}while(0)


#define SBS_MSG_prepare               (msg_type)1
#define SBS_MSG_start                 (msg_type)2
#define SBS_MSG_resume                (msg_type)3
#define SBS_MSG_flush                 (msg_type)4
#define SBS_MSG_stop                  (msg_type)5
#define SBS_MSG_reset                 (msg_type)6

BEGIN_MSG_LOOP(SubtitleSource)
    MSG_ITEM(SBS_MSG_prepare, onPrepare)
    MSG_ITEM(SBS_MSG_start, onStart)
    MSG_ITEM(SBS_MSG_resume, onResume)
    MSG_ITEM(SBS_MSG_flush, onFlush)
    MSG_ITEM(SBS_MSG_stop, onStop)
    MSG_ITEM(SBS_MSG_reset, onReset)
END_MSG_LOOP()

//////private class to parse subtitle file///////////

class SubtitleSource::ParseSrt: public SubtitleSource::Private {
  public:
    virtual mm_status_t parseFile(const char *uri) {
        if (!uri)
            FLEAVE_WITH_CODE(MM_ERROR_INVALID_URI);
        std::string uriStr = uri;
        std::ifstream infile(uriStr);
        if (!infile) {
            FLEAVE_WITH_CODE(MM_ERROR_OP_FAILED);
        }
        std::string line = "";
        int i = 0;
        bool eos = false;
        do {
            size_t found = std::string::npos;
            do {
                if(!getline(infile, line) || line.empty()) {
                    eos = true;
                    break;
                } else {
                    found = line.find(std::string("-->"));
                    if (found == std::string::npos) {
                        continue;
                    } else {
                        break;
                    }
                }
            } while(1);
            if (eos) break;
            // get time of subtitle like 00:01:57,880 --> 00:01:59,920
            SubtitleSource::SubtitleBufferSP buffer(new SubtitleSource::SubtitleBuffer());
            buffer->index = i++;
            parseTime(line.c_str(), found, buffer->startPositionMs, buffer->endPositionMs);
            do {
                // get all text of this subtitle
                if(!getline(infile, line) || line.size() <= 1) {
                    break;
                }
                if (buffer->text.size() > 0) {
                    buffer->text.append("\n");
                    buffer->text.append(line);
                } else {
                    buffer->text.append(line);
                }
            } while(1);
            mSubtitleSource->mSubtitleBuffers.push_back(buffer);
        } while(1);
        sortByStartTime();
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }
    ~ParseSrt() {}
    ParseSrt() {}
    MM_DISALLOW_COPY(ParseSrt);
};

class SubtitleSource::ParseSub: public SubtitleSource::Private {
  public:
    virtual mm_status_t parseFile(const char *uri) {
        if (!uri)
            FLEAVE_WITH_CODE(MM_ERROR_INVALID_URI);
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }
    ~ParseSub() {}
    ParseSub() {}
    MM_DISALLOW_COPY(ParseSub);
};

class SubtitleSource::ParseVTT: public SubtitleSource::Private {
  public:
      virtual mm_status_t parseFile(const char *uri) {
          if (!uri)
              FLEAVE_WITH_CODE(MM_ERROR_INVALID_URI);
          std::string uriStr = uri;
          std::ifstream infile(uriStr);
          if (!infile) {
              FLEAVE_WITH_CODE(MM_ERROR_OP_FAILED);
          }

          std::string line = "";
          int i = 0;
          bool eos = false;
          do {
              size_t found = std::string::npos;
              do {
                  if(!getline(infile, line) || line.empty()) {
                      eos = true;
                      break;
                  } else {
                      found = line.find(std::string("-->"));
                      if (found == std::string::npos) {
                          continue;
                      } else {
                          break;
                      }
                  }
              } while(1);
              if (eos) break;
              SubtitleSource::SubtitleBufferSP buffer(new SubtitleSource::SubtitleBuffer());
              buffer->index = i++;
              parseTime(line.c_str(), found, buffer->startPositionMs, buffer->endPositionMs);
              do {
                  // get all text of this subtitle
                  if(!getline(infile, line) || line.size() <= 1) {
                      break;
                  }
                  if (buffer->text.size() > 0) {
                      buffer->text.append("\n");
                      buffer->text.append(line);
                  } else {
                      buffer->text.append(line);
                  }
              } while(1);
              mSubtitleSource->mSubtitleBuffers.push_back(buffer);
          } while(1);
          sortByStartTime();
          EXIT_AND_RETURN(MM_ERROR_SUCCESS);
      }

    ~ParseVTT() {}
    ParseVTT() {}
    MM_DISALLOW_COPY(ParseVTT);
};

class SubtitleSource::ParseASS: public SubtitleSource::Private {
  public:
      virtual mm_status_t removeFont() {
          for (int i = 0; i < (int)mSubtitleSource->mSubtitleBuffers.size(); i++) {
              do {
                  size_t foundStart = std::string::npos;
                  size_t foundEnd = std::string::npos;
                  foundStart = mSubtitleSource->mSubtitleBuffers[i]->text.find('{');
                  foundEnd = mSubtitleSource->mSubtitleBuffers[i]->text.find('}');
                  if (foundStart != std::string::npos && foundEnd != std::string::npos && foundEnd > foundStart) {
                      mSubtitleSource->mSubtitleBuffers[i]->text.erase(mSubtitleSource->mSubtitleBuffers[i]->text.begin() + foundStart, mSubtitleSource->mSubtitleBuffers[i]->text.begin() + foundEnd + 1);
                  } else {
                      break;
                  }
              } while(1);
          }
          EXIT_AND_RETURN(MM_ERROR_SUCCESS);
      }

      virtual mm_status_t parseFile(const char *uri) {
          if (!uri)
              FLEAVE_WITH_CODE(MM_ERROR_INVALID_URI);
          std::string uriStr = uri;
          std::ifstream infile(uriStr);
          if (!infile) {
              FLEAVE_WITH_CODE(MM_ERROR_OP_FAILED);
          }

          std::string line = "";
          int i = 0;
          size_t found = std::string::npos;
          do {
              if(!getline(infile, line) || line.empty()) {
                  break;
              }
              uint64_t startMs = 0;
              uint64_t endMs = 0;
              // Format: Marked, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
              // Format: Layer, Start, End, Style, Actor, MarginL, MarginR, MarginV, Effect, Text
              // Dialogue: 0,0:02:50.01,0:02:51.74,*Default,NTP,0000,0000,0000,,Dad, it's doing it again.
              parseTime(line.c_str(), found, startMs, endMs);
              if (startMs == 0 && endMs == 0)continue;
              SubtitleSource::SubtitleBufferSP buffer(new SubtitleSource::SubtitleBuffer());
              buffer->index = i++;
              buffer->startPositionMs = startMs;
              buffer->endPositionMs = endMs;
              int commaCount = 9;
              int textPos = 0;
              for (textPos = 0; textPos < (int)line.length(); textPos++) {
                  if (line.at(textPos) == ',') {
                      if (!--commaCount) break;
                  }
              }
              if (textPos < (int)line.length()) {
                  buffer->text.append(line.c_str() + textPos + 1);
              } else {
                  buffer->text.append(" ");
              }

              mSubtitleSource->mSubtitleBuffers.push_back(buffer);
          } while(1);
          sortByStartTime();
          removeFont();
          EXIT_AND_RETURN(MM_ERROR_SUCCESS);
      }

    virtual void parseTime(const char *time, size_t pos, uint64_t& start, uint64_t& end) {
        int startHour = 0, startMin = 0, startSec = 0, startMs = 0;
        int endHour = 0, endMin = 0, endSec = 0, endMs = 0;
        int ret = sscanf(time,"Dialogue:%*[^,],%d:%d:%d%*1s%d,%d:%d:%d%*1s%d", &startHour, &startMin, &startSec, &startMs, &endHour, &endMin, &endSec, &endMs);
        if (ret != 8)return;
        start = (startSec + startMin * 60 + startHour * 3600) * 1000 + startMs * 10;
        end = (endSec + endMin * 60 + endHour * 3600) * 1000 + endMs * 10;
    }

    ~ParseASS() {}
    ParseASS() {}
    MM_DISALLOW_COPY(ParseASS);
};

class SubtitleSource::ParseTXT: public SubtitleSource::Private {
  public:
    enum TxtType {
        // 00:00:05.8:The No.1 subtitile:  Oh,hold on.
        // 00:00:08.1:
        TxtType_1,
        // {4618}{4695}Zamykamy drzwi.
        // {5049}{5118}Londyn 1903
        TxtType_2,
        TxtType_unknown
    };
    virtual mm_status_t removeFont2() {
          for (int i = 0; i < (int)mSubtitleSource->mSubtitleBuffers.size(); i++) {
              do {
                  size_t foundStart = std::string::npos;
                  size_t foundEnd = std::string::npos;
                  foundStart = mSubtitleSource->mSubtitleBuffers[i]->text.find('{');
                  foundEnd = mSubtitleSource->mSubtitleBuffers[i]->text.find('}');
                  if (foundStart != std::string::npos && foundEnd != std::string::npos && foundEnd > foundStart) {
                      mSubtitleSource->mSubtitleBuffers[i]->text.erase(mSubtitleSource->mSubtitleBuffers[i]->text.begin() + foundStart, mSubtitleSource->mSubtitleBuffers[i]->text.begin() + foundEnd + 1);
                  } else {
                      break;
                  }
              } while(1);
          }
          EXIT_AND_RETURN(MM_ERROR_SUCCESS);
      }
    virtual mm_status_t parseFile(const char *uri) {
        if (!uri)
            FLEAVE_WITH_CODE(MM_ERROR_INVALID_URI);
        std::string uriStr = uri;
        std::ifstream infile(uriStr);
        if (!infile) {
            FLEAVE_WITH_CODE(MM_ERROR_OP_FAILED);
        }

        std::string line = "";
        int i = 0;
        uint64_t startMs = 0;
        uint64_t endMs = 0;
        TxtType type = TxtType_unknown;
        if(!getline(infile, line) || line.empty()) {
            FLEAVE_WITH_CODE(MM_ERROR_OP_FAILED);
        }
        parseTimeType2(line.c_str(), 0, startMs, endMs);
        if (endMs > 0) {
            type = TxtType_2;
        }
        if (type == TxtType_unknown) {
            startMs = 0;
            parseTimeType1(line.c_str(), 0, startMs, endMs);
            if (startMs > 0)
                type = TxtType_1;
        }
        if (type == TxtType_1) {
            std::ifstream infile1(uriStr);
            do {
                if(!getline(infile, line) || line.empty()) {
                    break;
                }
                SubtitleSource::SubtitleBufferSP buffer(new SubtitleSource::SubtitleBuffer());
                buffer->index = i++;
                parseTimeType1(line.c_str(), 0, buffer->startPositionMs, buffer->endPositionMs);
                buffer->text.append(line.c_str() + 11);
                if(!getline(infile, line) || line.empty()) {
                    break;
                }
                parseTimeType1(line.c_str(), 0, buffer->endPositionMs, buffer->startPositionMs);
                mSubtitleSource->mSubtitleBuffers.push_back(buffer);
            } while(1);
        } else if (type == TxtType_2) {
                do {
                SubtitleSource::SubtitleBufferSP buffer(new SubtitleSource::SubtitleBuffer());
                buffer->index = i++;
                parseTimeType2(line.c_str(), 0, buffer->startPositionMs, buffer->endPositionMs);
                buffer->text.append(line.c_str());
                if(!getline(infile, line) || line.empty()) {
                    break;
                }
                mSubtitleSource->mSubtitleBuffers.push_back(buffer);
            } while(1);
            removeFont2();
        }
        sortByStartTime();
        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }

    virtual void parseTimeType1(const char *time, size_t pos, uint64_t& start, uint64_t& end) {
        int startHour = 0, startMin = 0, startSec = 0, startMs = 0;
        sscanf(time,"%d:%d:%d%*1s%d", &startHour, &startMin, &startSec, &startMs);
        start = (startSec + startMin * 60 + startHour * 3600) * 1000 + startMs * 100;
    }
    virtual void parseTimeType2(const char *time, size_t pos, uint64_t& start, uint64_t& end) {
        int startMs = 0, endMs = 0;
        sscanf(time,"{%d}{%d}", &startMs, &endMs);
        start = startMs / 25;
        end = endMs / 25;
    }
    ~ParseTXT() {}
    ParseTXT() {}
    MM_DISALLOW_COPY(ParseTXT);
};

class SubtitleSource::ParseSMI: public SubtitleSource::Private {
  public:
    virtual mm_status_t removeFont() {
      for (int i = 0; i < (int)mSubtitleSource->mSubtitleBuffers.size(); i++) {
          do { // remove &lt; ... &gt;
              size_t foundStart = std::string::npos;
              size_t foundEnd = std::string::npos;
              if (!mSubtitleSource->mSubtitleBuffers[i]->text.empty()) {
                  foundStart = mSubtitleSource->mSubtitleBuffers[i]->text.find("&lt;");
                  foundEnd = mSubtitleSource->mSubtitleBuffers[i]->text.find("&gt;");
                  if (foundStart != std::string::npos && foundEnd != std::string::npos && foundEnd > foundStart) {
                      mSubtitleSource->mSubtitleBuffers[i]->text.erase(mSubtitleSource->mSubtitleBuffers[i]->text.begin() + foundStart, mSubtitleSource->mSubtitleBuffers[i]->text.begin() + foundEnd + 4);
                  } else {
                      break;
                  }
                } else {
                    break;
                }
          } while(1);
          do { // remove &nbsp;
              size_t foundStart = std::string::npos;
              if (!mSubtitleSource->mSubtitleBuffers[i]->text.empty()) {
                  foundStart = mSubtitleSource->mSubtitleBuffers[i]->text.find("&nbsp;");
                  if (foundStart != std::string::npos) {
                      mSubtitleSource->mSubtitleBuffers[i]->text.erase(mSubtitleSource->mSubtitleBuffers[i]->text.begin() + foundStart, mSubtitleSource->mSubtitleBuffers[i]->text.begin() + foundStart + 6);
                  } else {
                      break;
                  }
              } else {
                  break;
              }
          } while(1);
          do {// remove < ... >
              size_t foundStart = std::string::npos;
              size_t foundEnd = std::string::npos;
              if (!mSubtitleSource->mSubtitleBuffers[i]->text.empty()) {
                  foundStart = mSubtitleSource->mSubtitleBuffers[i]->text.find('<');
                  foundEnd = mSubtitleSource->mSubtitleBuffers[i]->text.find('>');
                  if (foundStart != std::string::npos && foundEnd != std::string::npos && foundEnd > foundStart) {
                      mSubtitleSource->mSubtitleBuffers[i]->text.erase(mSubtitleSource->mSubtitleBuffers[i]->text.begin() + foundStart, mSubtitleSource->mSubtitleBuffers[i]->text.begin() + foundEnd + 1);
                  } else {
                      break;
                  }
              } else {
                  break;
              }
          } while(1);
      }
      EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }
    virtual mm_status_t parseFile(const char *uri) {
        if (!uri)
            FLEAVE_WITH_CODE(MM_ERROR_INVALID_URI);
        std::string uriStr = uri;
        std::ifstream infile(uriStr);
        if (!infile) {
            FLEAVE_WITH_CODE(MM_ERROR_OP_FAILED);
        }

        std::string line = "";
        int i = 0;
        bool eos = false;
        size_t found = std::string::npos;
        size_t found1 = std::string::npos;
        do {
            if(!getline(infile, line) || line.empty()) {
                eos = true;
                break;
            } else {
                found = line.find(std::string("<BODY>"));
                if (found == std::string::npos) {
                    continue;
                } else {
                    break;
                }
            }
        } while(1);
        if (eos) FLEAVE_WITH_CODE(MM_ERROR_OP_FAILED);
        do {
            do {
                found = line.find(std::string("<SYNC"));
                found1 = line.find(std::string("<sync"));
                if (found != std::string::npos || found1 != std::string::npos) {
                    break;
                }
                if(!getline(infile, line) || line.empty()) {
                    eos = true;
                    break;
                }
            } while(1);
            if (eos) break;

            found = line.find('=');
            if (found != std::string::npos) {
                // <sync start=5820>The No.11 subtitile:  Oh,hold on.
                // <SYNC Start=805762><P Class=KoreanSC>The No.11 subtitile:  Oh,hold on.
                SubtitleSource::SubtitleBufferSP buffer(new SubtitleSource::SubtitleBuffer());
                buffer->index = i++;
                parseTime(line.c_str() + found, 0, buffer->startPositionMs, buffer->endPositionMs);
                buffer->endPositionMs = buffer->startPositionMs + 5 * 1000;
                if (!mSubtitleSource->mSubtitleBuffers.empty()) {
                    uint64_t preStart = mSubtitleSource->mSubtitleBuffers[mSubtitleSource->mSubtitleBuffers.size() -1]->startPositionMs;
                    if (buffer->startPositionMs > preStart && (buffer->startPositionMs - preStart) < 10 * 1000)
                        mSubtitleSource->mSubtitleBuffers[mSubtitleSource->mSubtitleBuffers.size() -1]->endPositionMs = buffer->startPositionMs + 1;
                }
                buffer->text.append(line.c_str());
                do {
                    if(!getline(infile, line) || line.empty()) {
                        eos = true;
                        break;
                    }
                    found = line.find(std::string("<SYNC"));
                    found1 = line.find(std::string("<sync"));
                    if (found != std::string::npos || found1 != std::string::npos) {
                        break;
                    }
                    buffer->text.append(line.c_str());
                } while(1);

                mSubtitleSource->mSubtitleBuffers.push_back(buffer);
                if (eos) break;
            }

        } while(1);
        sortByStartTime();
        removeFont();

        EXIT_AND_RETURN(MM_ERROR_SUCCESS);
    }

    virtual void parseTime(const char *time, size_t pos, uint64_t& start, uint64_t& end) {
        int startMS = 0;
        sscanf(time,"=%d", &startMS);
        start = startMS;
    }
    ~ParseSMI() {}
    ParseSMI() {}
    MM_DISALLOW_COPY(ParseSMI);
};

void SubtitleSource::Private::parseTime(const char *time, size_t pos, uint64_t& start, uint64_t& end)
{
    int startHour = 0, startMin = 0, startSec = 0, startMs = 0;
    int endHour = 0, endMin = 0, endSec = 0, endMs = 0;
    if (pos == 13) {
        // %*1s to ignore separator ',' or '.'(VTT use '.' for separator and SRT use ',' for separator)
        // %*[ ] to ignore separator multi blanks.
        sscanf(time,"%d:%d:%d%*1s%d%*[ ]-->%*[ ]%d:%d:%d%*1s%d", &startHour, &startMin, &startSec, &startMs, &endHour, &endMin, &endSec, &endMs);
    } else {
        sscanf(time,"%d:%d%*1s%d%*[ ]-->%*[ ]%d:%d%*1s%d", &startMin, &startSec, &startMs, &endMin, &endSec, &endMs);
    }
    start = (startSec + startMin * 60 + startHour * 3600) * 1000 + startMs;
    end = (endSec + endMin * 60 + endHour * 3600) * 1000 + endMs;
}

SubtitleSource::PrivateSP SubtitleSource::Private::create(char* uri)
{
    ENTER();
    std::string uriStr = uri;
    size_t found;
    found = uriStr.find_last_of(".");
    if (found == std::string::npos) {
        INFO("invalid file path");
        return (PrivateSP((Private*)NULL));
    }
    if (!strcmp(uriStr.substr(found + 1).c_str(), "srt")) {
        PrivateSP priv(new SubtitleSource::ParseSrt());
        return priv;
    } else if (!strcmp(uriStr.substr(found + 1).c_str(), "idx")) {
        PrivateSP priv(new SubtitleSource::ParseSub());
        return priv;
    } else if (!strcmp(uriStr.substr(found + 1).c_str(), "vtt")) {
        PrivateSP priv(new SubtitleSource::ParseVTT());
        return priv;
    } else if (!strcmp(uriStr.substr(found + 1).c_str(), "txt")) {
        PrivateSP priv(new SubtitleSource::ParseTXT());
        return priv;
    } else if (!strcmp(uriStr.substr(found + 1).c_str(), "smi")) {
        PrivateSP priv(new SubtitleSource::ParseSMI());
        return priv;
    } else if (!strcmp(uriStr.substr(found + 1).c_str(), "ass") || !strcmp(uriStr.substr(found + 1).c_str(), "ssa")) {
        PrivateSP priv(new SubtitleSource::ParseASS());
        return priv;
    }
    return (PrivateSP((Private*)NULL));
}

mm_status_t SubtitleSource::Private::init(SubtitleSource *subtitlesource) {
    ENTER();
    mSubtitleSource = subtitlesource;
    EXIT_AND_RETURN(MM_ERROR_SUCCESS);
}


///////////////SubtitleSource::SubtitleSourceReader////////////////////
mm_status_t SubtitleSource::SubtitleSourceReader::read(MediaBufferSP & buffer)
{
    MMAutoLock locker(mSource->mLock);
    if (mSource->mBuffers.empty()) {
        return MM_ERROR_AGAIN;
    } else {
        buffer = mSource->mBuffers.front();
        mSource->mBuffers.pop();
    }

    return MM_ERROR_SUCCESS;
}

MediaMetaSP SubtitleSource::SubtitleSourceReader::getMetaData()
{
    ENTER();

    VERBOSE("now, read meta data:%d\n", mType);
    return ( MediaMetaSP((MediaMeta*)NULL) );
}

/////////////////SubtitleSource::ReaderThread////////////////////
SubtitleSource::ReaderThread::ReaderThread(SubtitleSource *source)
                                          : MMThread(MMTHREAD_NAME),
                                            mSource(source),
                                            mContinue(true)
{
    ENTER();
    FLEAVE();
}

SubtitleSource::ReaderThread::~ReaderThread()
{
    ENTER();
    FLEAVE();
}

void SubtitleSource::ReaderThread::signalExit()
{
    ENTER();
    MMAutoLock locker(mSource->mLock);
    mContinue = false;
    mSource->mCondition.signal();

    FLEAVE();
}

void SubtitleSource::ReaderThread::signalContinue()
{
    ENTER();
    mSource->mCondition.signal();
    FLEAVE();
}

/*static*/ bool SubtitleSource::ReaderThread::releaseInputBuffer(MediaBuffer* mediaBuffer)
{
    uint8_t *buffer = NULL;
    if (!(mediaBuffer->getBufferInfo((uintptr_t *)&buffer, NULL, NULL, 1))) {
        WARNING("error in release mediabuffer");
        return false;
    }
    MM_RELEASE_ARRAY(buffer);
    return true;
}

void SubtitleSource::ReaderThread::main()
{
    ENTER();

    while(1) {

        MMAutoLock locker(mSource->mLock);
        if (!mContinue) {
            break;
        }

        if (mSource->mIsPaused) {
           INFO("pause wait");
           mSource->mCondition.wait();
           INFO("pause wait wakeup");
        }
        if (mSource->mSubtitleBuffers.empty()) {
            INFO("no subbtitle buffer");
            mSource->mIsPaused = true;
            continue;
        }

        if (mSource->mCurrentIndex >= mSource->mSubtitleBuffers.size()) {
            MediaBufferSP srtBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
            if ( !srtBuffer.get() ) {
               VERBOSE("failed to createMediaBuffer\n");
               break;
            }
            srtBuffer->setFlag(MediaBuffer::MBFT_EOS);
            srtBuffer->setSize(0);
            mSource->mBuffers.push(srtBuffer);
            mSource->mIsPaused = true;
        }else {
            MediaBufferSP srtBuffer = MediaBuffer::createMediaBuffer(MediaBuffer::MBT_ByteBuffer);
            if ( !srtBuffer.get() ) {
               VERBOSE("failed to createMediaBuffer\n");
               break;
            }
            int bufferSize = mSource->mSubtitleBuffers[mSource->mCurrentIndex]->text.size();
            uint8_t *buffer = new uint8_t[bufferSize + 1];
            memcpy(buffer, mSource->mSubtitleBuffers[mSource->mCurrentIndex]->text.c_str(), bufferSize);
            *(buffer + bufferSize) = '\0';
            srtBuffer->setBufferInfo((uintptr_t *)&buffer, NULL, NULL, 1);
            srtBuffer->setPts(mSource->mSubtitleBuffers[mSource->mCurrentIndex]->startPositionMs * 1000ll);
            uint64_t duration = mSource->mSubtitleBuffers[mSource->mCurrentIndex]->endPositionMs - mSource->mSubtitleBuffers[mSource->mCurrentIndex]->startPositionMs;
            srtBuffer->setDuration(duration * 1000ll);
            srtBuffer->setSize(bufferSize);
            srtBuffer->addReleaseBufferFunc(releaseInputBuffer);
            mSource->mBuffers.push(srtBuffer);
            mSource->mCurrentIndex++;
        }
    }

    VERBOSE("Output thread exited\n");
    FLEAVE();
}

/////////////////////////SubtitleSource////////////////////////
SubtitleSource::SubtitleSource()
           :MMMsgThread(COMPONENT_NAME),
            mCondition(mLock),
            mIsPaused(true),
            mCurrentIndex(0)

{
    ENTER();
    mClockWrapper.reset(new ClockWrapper(ClockWrapper::kFlagVideoSink));
    FLEAVE();
}

SubtitleSource::~SubtitleSource()
{
    ENTER();
    FLEAVE();
}

mm_status_t SubtitleSource::setClock(ClockSP clock)
{
    ENTER();
    mm_status_t ret = mClockWrapper->setClock(clock);
    return ret;
}

mm_status_t SubtitleSource::init()
{
    ENTER();

    int ret = MMMsgThread::run(); // MMMsgThread->run();
    if (ret) {
        ERROR("subtitle source init error:%d\n", ret);
        FLEAVE_WITH_CODE(MM_ERROR_OP_FAILED);
    }

    FLEAVE_WITH_CODE( MM_ERROR_SUCCESS);
}

void SubtitleSource::uninit()
{
    ENTER();

    MMMsgThread::exit();

    FLEAVE();
}

mm_status_t SubtitleSource::createPriv()
{
    if (mUri.empty()) {
        ERROR("please call setUrl to set the mRtpURL\n");
        FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
    }

    PrivateSP priv = Private::create((char*)mUri.c_str());
    if (!priv) {
        INFO("create subtitle fail");
        FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
    }
    mPriv.reset();
    mPriv = priv;
    mPriv->init(this);

    releaseBuffers();
    mSubtitleBuffers.clear();
    mm_status_t ret = mPriv->parseFile(mUri.c_str());
    if (ret != MM_ERROR_SUCCESS)
        ERROR("subtitle file invalid, parse fail\n");

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

mm_status_t SubtitleSource::setUri(const char * uri, const std::map<std::string,
                                 std::string> * headers/* = NULL*/)
{
    ENTER();

    MMAutoLock locker(mLock);
    INFO("uri: %s\n", uri);
    if (!uri)
        FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
    mUri = uri;

    createPriv();
    int64_t currentPosition = -1ll;
    if (mClockWrapper && mClockWrapper->getCurrentPosition(currentPosition) != MM_ERROR_SUCCESS) {
        ERROR("getCurrentPosition failed");
        currentPosition = -1ll;
    }
    int index = 0;
    currentPosition /= 1000;
    for (index = 0; index < (int)mSubtitleBuffers.size(); index++)
    {
        if (currentPosition < (int64_t)mSubtitleBuffers[index]->endPositionMs) {
            mCurrentIndex = index;
            break;
        }
    }
    if (!mReaderThread) {
        mReaderThread.reset (new ReaderThread(this), MMThread::releaseHelper);
        mReaderThread->create();
    }
    mIsPaused = false;

    mReaderThread->signalContinue();

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

const std::list<std::string> & SubtitleSource::supportedProtocols() const
{
    static std::list<std::string> protocols;
    return protocols;
}

mm_status_t SubtitleSource::prepare()
{
    ENTER();

    postMsg(SBS_MSG_prepare, 0, NULL);

    FLEAVE_WITH_CODE(MM_ERROR_ASYNC);
}

void SubtitleSource::onPrepare(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();

    MMAutoLock locker(mLock);
    if (mUri.empty()) {
        ERROR("please call setUrl to set the mRtpURL\n");
        notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
        FLEAVE();
    }

    notify(kEventPrepareResult, MM_ERROR_SUCCESS, 0, nilParam);
    FLEAVE();
}

mm_status_t SubtitleSource::start()
{
    ENTER();

    postMsg(SBS_MSG_start, 0, NULL);

    FLEAVE_WITH_CODE(MM_ERROR_ASYNC);
}

void SubtitleSource::onStart(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();

    MMAutoLock locker(mLock);

    mIsPaused = false;
    if (mReaderThread)
        mReaderThread->signalContinue();
    notify(kEventStartResult, MM_ERROR_SUCCESS, 0, nilParam);
    FLEAVE();
}

mm_status_t SubtitleSource::flush()
{
    ENTER();

    postMsg(SBS_MSG_flush, 0, NULL);

    FLEAVE_WITH_CODE(MM_ERROR_ASYNC);
}

void SubtitleSource::onFlush(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();

    MMAutoLock locker(mLock);
    releaseBuffers();
    notify(kEventFlushComplete, MM_ERROR_SUCCESS, 0, nilParam);
    FLEAVE();
}

mm_status_t SubtitleSource::resume()
{
    ENTER();

    postMsg(SBS_MSG_resume, 0, NULL);

    FLEAVE_WITH_CODE(MM_ERROR_ASYNC);

}

void SubtitleSource::onResume(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();

    MMAutoLock locker(mLock);

    mIsPaused = false;

    if (mReaderThread)
        mReaderThread->signalContinue();
    notify(kEventResumed, MM_ERROR_SUCCESS, 0, nilParam);
    FLEAVE();
}

mm_status_t SubtitleSource::stop()
{
    ENTER();

    postMsg(SBS_MSG_stop, 0, NULL);

    FLEAVE_WITH_CODE(MM_ERROR_ASYNC);
}

void SubtitleSource::releaseBuffers()
{
    while(!mBuffers.empty())
        mBuffers.pop();
}

mm_status_t SubtitleSource::internalStop()
{
    ENTER();

    mIsPaused = true;
    if (mReaderThread) {
        mReaderThread->signalExit();
    }

    {
        MMAutoLock locker(mLock);
        releaseBuffers();
        mSubtitleBuffers.clear();
     }

    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

void  SubtitleSource::onStop(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    mm_status_t ret = internalStop();
    notify(kEventStopped, ret, 0, nilParam);
    FLEAVE();
}

mm_status_t SubtitleSource::reset()
{
    ENTER();

    postMsg(SBS_MSG_reset, 0, NULL);

    FLEAVE_WITH_CODE(MM_ERROR_ASYNC);
}

void SubtitleSource::onReset(param1_type param1, param2_type param2, uint32_t rspId)
{
    ENTER();
    mm_status_t ret = internalStop();
    notify(kEventResetComplete, ret, 0, nilParam);

    FLEAVE();
}

mm_status_t SubtitleSource::seek(int msec, int seekSequence)
{
    MMAutoLock locker(mLock);
    mCurrentIndex = mSubtitleBuffers.size();
    int index = 0;
    for (index = 0; index < (int)mSubtitleBuffers.size(); index++)
    {
        if (msec < (int)mSubtitleBuffers[index]->endPositionMs) {
            mCurrentIndex = index;
            break;
        }
    }
    notify(kEventSeekComplete, MM_ERROR_SUCCESS, 0, nilParam);
    FLEAVE_WITH_CODE(MM_ERROR_SUCCESS);
}

Component::ReaderSP SubtitleSource::getReader(MediaType mediaType)
{
    ENTER();
    if ( mediaType != Component::kMediaTypeSubtitle ) {
        MMLOGE("not supported mediatype: %d\n", mediaType);
        return Component::ReaderSP((Component::Reader*)NULL);
    }

    return Component::ReaderSP(new SubtitleSource::SubtitleSourceReader(this, Component::kMediaTypeSubtitle));
}


} // YUNOS_MM

/////////////////////////////////////////////////////////////////////////////////////
extern "C" {

YUNOS_MM::Component* createComponent(const char* mimeType, bool isEncoder) {
    YUNOS_MM::SubtitleSource *sourceComponent = new YUNOS_MM::SubtitleSource();
    if (sourceComponent == NULL) {
        return NULL;
    }
    return static_cast<YUNOS_MM::Component*>(sourceComponent);
}

void releaseComponent(YUNOS_MM::Component *component) {
    delete component;
}

}

