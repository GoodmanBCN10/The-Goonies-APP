import os
with open('source/ui/settings/settings_view.hpp', 'r', encoding='utf-8') as f:
    code = f.read()

code = code.replace('''                const std::string temp = updater_->stagedPath();
                const std::string target = updater_->targetPath();
                const std::string arguments = "\"" + temp + "\" \"--finish-update\" \"" + target + "\"";
                const Result result = envSetNextLoad(temp.c_str(),
                                                     arguments.c_str());''', '''                const std::string helper = updater_->helperPath();
                const std::string arguments = helper + " --finish-update";
                const Result result = envSetNextLoad(helper.c_str(),
                                                     arguments.c_str());''')

with open('source/ui/settings/settings_view.hpp', 'w', encoding='utf-8') as f:
    f.write(code)

