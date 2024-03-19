#ifndef __STATE_HPP__
#define __STATE_HPP__

#include <stackmat.h>
#include <UUID.h>
#include "ws_logger.h"
#include "translations.h"
#include "lcd.hpp"

#define UUID_LENGTH 37
String displayTime(uint8_t m, uint8_t s, uint16_t ms);
void sendSolve(bool delegate);

UUID uuid;
bool stateHasChanged = true;
bool lockStateChange = false;
bool waitForSolveResponse = false;

enum StateScene {
  SCENE_NOT_INITALIZED, // before timer connects to wifi/ws
  SCENE_WAITING_FOR_COMPETITOR, // before competitor scans card
  SCENE_COMPETITOR_INFO, // competitor info with inspection info

  // FROM HERE, DO NOT SHOW TIMER/SERVER DISCONNECTED
  SCENE_INSPECTION, // during inspection (show inspection time etc)
  SCENE_TIMER_TIME, // during solve
  SCENE_FINISHED_TIME, // after solve
  SCENE_ERROR // after error
};

struct State {
    StateScene currentScene = SCENE_NOT_INITALIZED;

    char solveSessionId[UUID_LENGTH];
    int solveTime = 0;
    int penalty = 0;

    unsigned long competitorCardId = 0;
    unsigned long judgeCardId = 0;
    char competitorDisplay[128]; // max 128 chars

    bool timeConfirmed = false;
    bool waitingForSolveResponse = false;

    StackmatTimerState lastTimerState = ST_Unknown;
    bool stackmatConnected = false;
} state;

// TODO: save state to EEPROM
struct EEPROMState {
    char solveSessionId[UUID_LENGTH];
    unsigned long competitorCardId;
    int solveTime;
    int penalty;
};

void initState() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Logger.println("Failed to obtain time");
  }
  time_t epoch;
  time(&epoch);

  uuid.seed(epoch, (unsigned long)ESP.getEfuseMac());

  state.currentScene = SCENE_WAITING_FOR_COMPETITOR;
}

/// @brief Called after time is finished
/// @param solveTime 
void startSolveSession(int solveTime) {
    uuid.generate(); // generate next uuid

    strncpy(state.solveSessionId, uuid.toCharArray(), UUID_LENGTH);
    state.solveTime = solveTime;
    state.penalty = 0;
    state.judgeCardId = 0;
    state.timeConfirmed = false;
    waitForSolveResponse = false;
    state.currentScene = SCENE_FINISHED_TIME;

    stateHasChanged = true;
}

void resetSolveState() {
    state.solveTime = 0;
    state.penalty = 0;
    state.judgeCardId = 0;
    state.timeConfirmed = false;
    memset(state.competitorDisplay, ' ', sizeof(state.competitorCardId));
    waitForSolveResponse = false;
    state.currentScene = SCENE_WAITING_FOR_COMPETITOR;

    stateHasChanged = true;
}

void lcdStateManagementLoop() {
    if(!stateHasChanged || lockStateChange) return;

    if (state.currentScene == SCENE_FINISHED_TIME) {
        if (waitForSolveResponse) {
            lcdPrintf(0, true, ALIGN_CENTER, TR_WAITING_FOR_SOLVE_TOP);
            lcdPrintf(1, true, ALIGN_CENTER, TR_WAITING_FOR_SOLVE_BOTTOM);

            stateHasChanged = false;
            return;
        }

        uint8_t minutes = state.solveTime / 60000;
        uint8_t seconds = (state.solveTime % 60000) / 1000;
        uint16_t ms = state.solveTime % 1000;

        /* Line 1 */
        lcdPrintf(0, true, ALIGN_LEFT, "%s", displayTime(minutes, seconds, ms).c_str());
        if (state.penalty == -1) {
            lcdPrintf(0, false, ALIGN_RIGHT, "DNF");
        } else if(state.penalty > 0) {
            lcdPrintf(0, false, ALIGN_RIGHT, "+%d", state.penalty);
        }

        /* Line 2 */
        if (!state.timeConfirmed) {
            lcdPrintf(1, true, ALIGN_RIGHT, TR_CONFIRM_TIME);
        } else if (state.judgeCardId == 0) {
            lcdPrintf(1, true, ALIGN_RIGHT, TR_AWAITING_JUDGE);
        } else if(state.judgeCardId > 0 && state.competitorCardId > 0) {
            lcdPrintf(1, true, ALIGN_RIGHT, TR_AWAITING_COMPETITOR_AGAIN);
        }
    }

    stateHasChanged = false;
}

String displayTime(uint8_t m, uint8_t s, uint16_t ms) {
  String tmp = "";
  if (m > 0) {
    tmp += m;
    tmp += ":";

    char sBuff[6];
    sprintf(sBuff, "%02d", s);
    tmp += String(sBuff);
  } else {
    tmp += s;
  }

  char msBuff[6];
  sprintf(msBuff, "%03d", ms);

  tmp += ".";
  tmp += String(msBuff);
  return tmp;
}

void sendSolve(bool delegate) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Logger.println("Failed to obtain time");
  }
  time_t epoch;
  time(&epoch);

  JsonDocument doc;
  doc["solve"]["solve_time"] = state.solveTime;
  doc["solve"]["penalty"] = state.penalty;
  doc["solve"]["competitor_id"] = state.competitorCardId;
  doc["solve"]["judge_id"] = state.judgeCardId;
  doc["solve"]["esp_id"] = (unsigned long)ESP.getEfuseMac();
  doc["solve"]["timestamp"] = epoch;
  doc["solve"]["session_id"] = state.solveSessionId;
  doc["solve"]["delegate"] = delegate;

  String json;
  serializeJson(doc, json);
  webSocket.sendTXT(json);

  waitForSolveResponse = true;
}

#endif