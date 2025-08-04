// AmiClientCommandDef.hpp
#pragma once
#include <string>
#include <optional>
#include <initializer_list>
#include <vector>




namespace ami {
    /**
     * Definition of a command for AMI Client.
     * Mirrors Java's AmiClientCommandDef.
     */
    class AmiClientCommandDef {
    public:
        // Predefined condition constants
        static inline const std::string CONDITION_USER_LOGIN = "user_open_layout";
        static inline const std::string CONDITION_USER_LOGOUT = "user_close_layout";
        static inline const std::string CONDITION_USER_CLICK = "user_click";
        static inline const std::string CONDITION_NOW = "now";

        explicit AmiClientCommandDef(std::string id);

        // Getters
        const std::string& getCommandId() const;
        const std::optional<int>& getLevel() const;
        const std::optional<std::string>& getWhereClause() const;
        const std::optional<std::string>& getHelp() const;
        const std::optional<std::string>& getArgumentsJson() const;
        const std::optional<std::string>& getName() const;
        const std::optional<int>& getPriority() const;
        const std::optional<std::string>& getEnabledExpression() const;
        const std::optional<std::string>& getStyle() const;
        const std::optional<std::string>& getSelectMode() const;
        const std::optional<std::string>& getFields() const;
        const std::optional<std::string>& getFilterClause() const;
        const std::optional<std::string>& getConditions() const;
        const std::optional<std::string>& getAmiScript() const;

        // Fluent setters
        AmiClientCommandDef& setCommandId(std::string id);
        AmiClientCommandDef& setLevel(int level);
        AmiClientCommandDef& setWhereClause(std::string where);
        AmiClientCommandDef& setHelp(std::string help);
        AmiClientCommandDef& setArgumentsJson(std::string argsJson);
        AmiClientCommandDef& setName(std::string name);
        AmiClientCommandDef& setPriority(int priority);
        AmiClientCommandDef& setEnabledExpression(std::string expr);
        AmiClientCommandDef& setStyle(std::string style);
        AmiClientCommandDef& setSelectMode(int min, int max);
        AmiClientCommandDef& setFields(std::string fields);
        AmiClientCommandDef& setFilterClause(std::string clause);
        AmiClientCommandDef& setConditions(std::initializer_list<std::string> conds);
        AmiClientCommandDef& setAmiScript(std::string script);

    private:
        std::string cmdId_;
        std::optional<int> level_;
        std::optional<std::string> whereClause_, help_, argumentsJson_, name_;
        std::optional<int> priority_;
        std::optional<std::string> enabledExpression_, style_, selectMode_;
        std::optional<std::string> fields_, filterClause_, conditions_, amiScript_;
    };

}


