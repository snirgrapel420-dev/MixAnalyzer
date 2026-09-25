#pragma once
// ============================================================================
//  Production Analyzer - ReportBuilder
//  Turns Metrics into a Hebrew report: status, explanation, critical tip,
//  practical tips, and a pre-master checklist. Strings are UTF-8.
//  All thresholds live in kProfiles / the check functions in the .cpp.
// ============================================================================
#include "AnalysisEngine.h"
#include <string>
#include <vector>

namespace pa
{

enum class Status { Ok = 0, Info = 1, Warning = 2, Problem = 3 };

struct Finding
{
    std::string id;
    std::string title;                 // Hebrew
    Status      status = Status::Ok;
    std::string value;                 // LTR numbers line
    std::string explanation;           // Hebrew
    std::string criticalTip;           // the one thing to do first (may be empty)
    std::vector<std::string> tips;     // Hebrew
};

struct ChecklistItem
{
    std::string text;                  // Hebrew
    Status      status = Status::Info; // Ok = done, Problem = fix, Info = check manually
};

struct Report
{
    bool valid = false;
    int  score = 0;                    // 0..100
    std::string headline;              // Hebrew, one line
    std::string summary;               // Hebrew
    std::vector<std::string> priorities;   // top things to fix, ordered
    std::vector<Finding> findings;         // sorted: Problem, Warning, Info, Ok
    std::vector<ChecklistItem> checklist;  // pre-master delivery checklist
};

int                      getNumProfiles();
std::string              getProfileName (int index);
Report                   buildReport (const Metrics& m, int profileIndex);
std::string              reportToText (const Report& r, const Metrics& m, int profileIndex);
const char*              statusName (Status s);   // Hebrew label

} // namespace pa
