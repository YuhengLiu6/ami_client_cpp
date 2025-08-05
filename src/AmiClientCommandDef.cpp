// AmiClientCommandDef.cpp
#include <AmiClientCpp/AmiClientCommandDef.hpp>
#include <algorithm>



namespace ami {
    AmiClientCommandDef::AmiClientCommandDef(std::string id)
        : cmdId_(std::move(id)) {
    }

    const std::string& AmiClientCommandDef::getCommandId() const { return cmdId_; }
    const std::optional<int>& AmiClientCommandDef::getLevel() const { return level_; }
    const std::optional<std::string>& AmiClientCommandDef::getWhereClause() const { return whereClause_; }
    const std::optional<std::string>& AmiClientCommandDef::getHelp() const { return help_; }
    const std::optional<std::string>& AmiClientCommandDef::getArgumentsJson() const { return argumentsJson_; }
    const std::optional<std::string>& AmiClientCommandDef::getName() const { return name_; }
    const std::optional<int>& AmiClientCommandDef::getPriority() const { return priority_; }
    const std::optional<std::string>& AmiClientCommandDef::getEnabledExpression() const { return enabledExpression_; }
    const std::optional<std::string>& AmiClientCommandDef::getStyle() const { return style_; }
    const std::optional<std::string>& AmiClientCommandDef::getSelectMode() const { return selectMode_; }
    const std::optional<std::string>& AmiClientCommandDef::getFields() const { return fields_; }
    const std::optional<std::string>& AmiClientCommandDef::getFilterClause() const { return filterClause_; }
    const std::optional<std::string>& AmiClientCommandDef::getConditions() const { return conditions_; }
    const std::optional<std::string>& AmiClientCommandDef::getAmiScript() const { return amiScript_; }

    AmiClientCommandDef& AmiClientCommandDef::setCommandId(std::string id) {
        cmdId_ = std::move(id);
        return *this;
    }
    AmiClientCommandDef& AmiClientCommandDef::setLevel(int level) {
        level_ = level;
        return *this;
    }
    AmiClientCommandDef& AmiClientCommandDef::setWhereClause(std::string where) {
        whereClause_ = std::move(where);
        return *this;
    }
    AmiClientCommandDef& AmiClientCommandDef::setHelp(std::string help) {
        help_ = std::move(help);
        return *this;
    }
    AmiClientCommandDef& AmiClientCommandDef::setArgumentsJson(std::string argsJson) {
        argumentsJson_ = std::move(argsJson);
        return *this;
    }
    AmiClientCommandDef& AmiClientCommandDef::setName(std::string name) {
        name_ = std::move(name);
        return *this;
    }
    AmiClientCommandDef& AmiClientCommandDef::setPriority(int priority) {
        priority_ = priority;
        return *this;
    }
    AmiClientCommandDef& AmiClientCommandDef::setEnabledExpression(std::string expr) {
        enabledExpression_ = std::move(expr);
        return *this;
    }
    AmiClientCommandDef& AmiClientCommandDef::setStyle(std::string style) {
        style_ = std::move(style);
        return *this;
    }
    AmiClientCommandDef& AmiClientCommandDef::setSelectMode(int min, int max) {
        if (min == max) {
            selectMode_ = std::to_string(min);
        }
        else if (min < 0) {
            selectMode_ = "-" + std::to_string(max);
        }
        else if (max < 0) {
            selectMode_ = std::to_string(min) + "-";
        }
        else {
            selectMode_ = std::to_string(min) + "-" + std::to_string(max);
        }
        return *this;
    }
    AmiClientCommandDef& AmiClientCommandDef::setFields(std::string fields) {
        fields_ = std::move(fields);
        return *this;
    }
    AmiClientCommandDef& AmiClientCommandDef::setFilterClause(std::string clause) {
        filterClause_ = std::move(clause);
        return *this;
    }
    AmiClientCommandDef& AmiClientCommandDef::setConditions(std::initializer_list<std::string> conds) {
        if (conds.size() == 0) {
            conditions_.reset();
        }
        else {
            std::string s;
            for (auto& c : conds) {
                if (!s.empty()) s += ',';
                s += c;
            }
            conditions_ = std::move(s);
        }
        return *this;
    }
    AmiClientCommandDef& AmiClientCommandDef::setAmiScript(std::string script) {
        amiScript_ = std::move(script);
        return *this;
    }


}