#pragma once

#include <QString>
#include <QtGlobal>

enum class ProductionMeasureMode : int
{
    Normal = 0,
    Second = 1,
    Third = 2,
    Mil = 3
};

enum class SlotRuntimeState : int
{
    Empty = 0,
    Loaded,
    WaitingIdCheck,
    ScanMismatch,
    Measuring,
    WaitingPcRead,
    Ok,
    Ng,
    Calibration,
    Unknown
};

struct SlotMeasureSummary
{
    QChar part_type = QChar('A');   // 'A' or 'B'
    bool valid = false;             // 是否已有计算结果
    bool judgement_known = false;   // 上位机是否已得出 OK/NG
    bool judgement_ok = false;      // true=OK false=NG
    QString fail_reason_text;       // 由上位机算法/业务层给出

    // A 型
    float a_total_len_mm = qQNaN();
    float a_id_left_mm  = qQNaN();
    float a_od_left_mm  = qQNaN();
    float a_id_right_mm = qQNaN();
    float a_od_right_mm = qQNaN();

    // B 型
    float b_ad_len_mm      = qQNaN();
    float b_bc_len_mm      = qQNaN();
    float b_runout_left_mm = qQNaN();
    float b_runout_right_mm= qQNaN();
};
