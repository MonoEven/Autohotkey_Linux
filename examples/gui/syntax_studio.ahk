#Requires AutoHotkey v2.0
#ErrorStdOut UTF-8
#SingleInstance Force
Persistent

; A runnable, self-contained syntax course using the Linux-tested GUI controls.
; Code examples use @@ as a display placeholder for a double quote.
Code(text) => StrReplace(text, "@@", Chr(34))

class Lesson {
    __New(id, title, level, summary, syntax, example, exercise, tip, checkWords) {
        this.id := id
        this.title := title
        this.level := level
        this.summary := summary
        this.syntax := syntax
        this.example := example
        this.exercise := exercise
        this.tip := tip
        this.checkWords := checkWords
    }
}

lesson1 := Lesson(1, "Variables & expressions", "Beginner", "Store values, combine text, and calculate results.", Code("name := value`nresult := expression`ntext := @@Hello @@ name`nnumber += 1"), Code("#Requires AutoHotkey v2.0`n`nname := @@Ada@@`nvisits := 3`nmessage := @@Hello @@ name`nFileAppend(message @@ (visits + 1), A_Temp @@/ahk-studio-vars.txt@@)"), Code("name := @@Ada@@`nvisits := 3`nFileAppend(name @@ has @@ (visits + 1) @@ visits@@, A_Temp @@/ahk-studio-exercise.txt@@)"), "Use := for assignment. Parentheses make the intended order obvious. Strings use double quotes.", ["FileAppend", ":=", "visits"])

lesson2 := Lesson(2, "Conditions", "Beginner", "Choose a path with if/else and compare values safely.", Code("if condition`n    statement`nelse`n    statement`n`nif value = 0`n    ..."), Code("#Requires AutoHotkey v2.0`n`nscore := 82`nif score >= 60`n    result := @@pass@@`nelse`n    result := @@retry@@`nFileAppend(result, A_Temp @@/ahk-studio-condition.txt@@)"), Code("score := 72`nif score >= 60`n    result := @@pass@@`nelse`n    result := @@retry@@`nFileAppend(result, A_Temp @@/ahk-studio-exercise.txt@@)"), "Use = for comparison and := for assignment. Keep the condition readable.", ["if", "else", ">=", "FileAppend"])

loopSyntax := "Loop count" . Chr(10) . "    A_Index" . Chr(10) . "" . Chr(10) . "for item in items" . Chr(10) . "    ..."
loopExample := "#Requires AutoHotkey v2.0" . Chr(10) . Chr(10) . "lines := " . Chr(34) . Chr(34) . Chr(10) . "Loop 4" . Chr(10) . "    lines .= A_Index" . Chr(10) . "FileAppend(lines, A_Temp " . Chr(34) . "/ahk-studio-loop.txt" . Chr(34) . ")"
loopExercise := "total := 0" . Chr(10) . "Loop 5" . Chr(10) . "    total += A_Index" . Chr(10) . "FileAppend(" . Chr(34) . "total=" . Chr(34) . " total, A_Temp " . Chr(34) . "/ahk-studio-exercise.txt" . Chr(34) . ")"
lesson3 := Lesson(3, "Loops", "Beginner", "Repeat work with Loop and inspect A_Index for the current iteration.", loopSyntax, loopExample, loopExercise, "Loop is ideal for a count. For collections, use for item in collection.", ["Loop", "A_Index", ".=", "FileAppend"])

lesson4 := Lesson(4, "Functions", "Intermediate", "Package reusable behavior with parameters and return values.", Code("BuildLabel(name, count) {`n    return name @@ x@@ count`n}`n`nlabel := BuildLabel(@@A@@, 3)"), Code("#Requires AutoHotkey v2.0`n`nDescribe(name, count) {`n    return name @@ has @@ count @@ items@@`n}`n`ntext := Describe(@@Lessons@@, 4)`nFileAppend(text, A_Temp @@/ahk-studio-function.txt@@)"), Code("Multiply(a, b) {`n    return a * b`n}`nanswer := Multiply(6, 7)`nFileAppend(@@answer=@@ answer, A_Temp @@/ahk-studio-exercise.txt@@)"), "A function should do one clear thing. return sends a value back to the caller.", ["Multiply", "return", "answer", "FileAppend"])

lesson5 := Lesson(5, "Arrays, Maps & objects", "Intermediate", "Group related data and retrieve it by index or key.", Code("items := [@@red@@, @@green@@]`nsettings := Map(@@theme@@, @@dark@@)`nsettings[@@theme@@]"), Code("#Requires AutoHotkey v2.0`n`ncolors := [@@teal@@, @@amber@@, @@navy@@]`nsettings := Map(@@course@@, @@AHK v2@@, @@lessons@@, colors.Length)`nFileAppend(settings[@@course@@] @@ / @@ settings[@@lessons@@], A_Temp @@/ahk-studio-object.txt@@)"), Code("profile := Map(@@name@@, @@Learner@@, @@level@@, 2)`nFileAppend(profile[@@name@@] @@ level=@@ profile[@@level@@], A_Temp @@/ahk-studio-exercise.txt@@)"), "Arrays are ordered. Maps use keys. Object properties model a record clearly.", ["Map", "profile", "[", "FileAppend"])

lesson6 := Lesson(6, "Errors & exceptions", "Intermediate", "Use try/catch to make failures visible and recoverable.", Code("try {`n    risky operation`n} catch as err {`n    err.Message`n}`n`nthrow Error(@@message@@)"), Code("#Requires AutoHotkey v2.0`n`ntry {`n    if !FileExist(A_Temp @@/missing-input.txt@@)`n        throw Error(@@Input file is missing@@)`n} catch as err {`n    FileAppend(@@handled: @@ err.Message, A_Temp @@/ahk-studio-error.txt@@)`n}"), Code("try {`n    throw Error(@@Practice error@@)`n} catch as err {`n    FileAppend(err.Message, A_Temp @@/ahk-studio-exercise.txt@@)`n}"), "Catch the specific error object with catch as err. Messages should help recovery.", ["try", "catch", "Error", "err.Message"])

lesson7 := Lesson(7, "Hotkeys", "Advanced", "Bind a physical key combination to a function or block of code.", Code("^!j::MsgBox(@@Ctrl+Alt+J@@)`n`nF1::{`n    ToolTip(@@Pressed@@)`n}`n`n#HotIf condition`nF2::..."), Code("#Requires AutoHotkey v2.0`n`nF1::{`n    FileAppend(@@F1 pressed``n@@, A_Temp @@/ahk-studio-hotkey.txt@@)`n}`n`n^!j::MsgBox(@@Ctrl+Alt+J@@)"), Code("F2::{`n    FileAppend(@@custom hotkey``n@@, A_Temp @@/ahk-studio-exercise.txt@@)`n}"), "A hotkey body can be a single expression or a braced block. Use #HotIf for context.", ["F2", "::", "FileAppend", "#HotIf"])

lesson8 := Lesson(8, "Hotstrings & InputHook", "Advanced", "Expand abbreviations and capture text with controlled input semantics.", Code(":*:btw::by the way`n`ninput := InputHook(@@V T3@@, @@ @@, @@{Enter}@@)`ninput.Start()`ninput.Wait()"), Code("#Requires AutoHotkey v2.0`n`n:*:sig::{`n    SendText(@@Best regards,``nAda@@)`n}`n`ninput := InputHook(@@V T3@@, @@ @@, @@{Enter}@@)`ninput.Start()`ninput.Wait()"), Code(":*:omw::on my way`n`ninput := InputHook(@@V T2@@, @@ @@, @@{Enter}@@)`ninput.Start()`ninput.Wait()`nFileAppend(input.Input, A_Temp @@/ahk-studio-exercise.txt@@)"), "Hotstrings match text. InputHook controls visibility, timeout, end keys, and callbacks.", [":*:", "InputHook", ".Start", ".Wait"])

lesson9 := Lesson(9, "GUI events", "Advanced", "Create a window and connect controls to event callbacks.", Code("window := Gui(, @@Title@@)`nbutton := window.AddButton(, @@Run@@)`nbutton.OnEvent(@@Click@@, Handler)`nwindow.Show()"), Code("#Requires AutoHotkey v2.0`n`nwindow := Gui(, @@Mini lesson@@)`nwindow.AddText(, @@Click the button@@)`nbutton := window.AddButton(, @@Run@@)`nbutton.OnEvent(@@Click@@, (*) => ToolTip(@@Event received@@))`nwindow.OnEvent(@@Close@@, (*) => ExitApp())`nwindow.Show()"), Code("window := Gui(, @@Practice@@)`nbutton := window.AddButton(, @@Write result@@)`nbutton.OnEvent(@@Click@@, (*) => FileAppend(@@clicked@@, A_Temp @@/ahk-studio-exercise.txt@@))`nwindow.Show()"), "Create controls first, then attach OnEvent handlers. Always provide a Close handler.", ["Gui", "AddButton", "OnEvent", ".Show"])

lesson10 := Lesson(10, "Files & processes", "Advanced", "Connect syntax to useful automation, files, commands, and exit codes.", Code("FileAppend(text, path)`ncontent := FileRead(path)`nRunWait(command, , @@Hide@@)`nExitApp(code)"), Code("#Requires AutoHotkey v2.0`n`npath := A_Temp @@/ahk-studio-file.txt@@`nFileAppend(@@first line``n@@, path)`ncontent := FileRead(path)`nFileAppend(@@bytes=@@ StrLen(content), A_Temp @@/ahk-studio-process.txt@@)"), Code("path := A_Temp @@/ahk-studio-exercise.txt@@`nFileAppend(@@saved``n@@, path)`ncontent := FileRead(path)`nFileAppend(@@length=@@ StrLen(content), path)"), "Use A_Temp for disposable teaching output. Check RunWait's exit code when a process matters.", ["FileAppend", "FileRead", "A_Temp", "RunWait"])

lessons := [lesson1, lesson2, lesson3, lesson4, lesson5, lesson6, lesson7, lesson8, lesson9, lesson10]
visibleIds := []
lastLessonSelection := 0
for entry in lessons
    visibleIds.Push(entry.id)

window := Gui("+Resize", "AHK v2 Syntax Studio")
window.BackColor := "F4F7FA"
window.MarginX := 18
window.MarginY := 14
window.SetFont("s10", "Sans")

window.AddText("x18 y14 w1180 h34 c17324D", "AHK v2 SYNTAX STUDIO")
window.AddText("x18 y45 w760 h22 c526579", "A hands-on course for learning the language through small, runnable patterns.")
progressText := window.AddText("x920 y18 w270 h24 Right c0B7285", "Lesson 1 of 10")
window.AddText("x18 y76 w250 h18 c526579", "COURSE MAP")
window.AddText("x286 y76 w560 h18 c526579", "LESSON WORKBENCH")
window.AddText("x868 y76 w322 h18 c526579", "CONCEPT MAP")

search := window.AddEdit("x18 y98 w250 h30", "")
search.OnEvent("Change", FilterLessons)
level := window.AddDropDownList("x18 y136 w250 h28", ["All levels", "Beginner", "Intermediate", "Advanced"])
level.Value := 1
level.OnEvent("Change", FilterLessons)
lessonList := window.Add("ListBox", "x18 y174 w250 h430", ["Loading lessons..."])

lessonList.OnEvent("Change", LoadLesson)

lessonTitle := window.AddText("x286 y98 w560 h30 c17324D", "")
lessonMeta := window.AddText("x286 y130 w560 h22 c0B7285", "")
summary := window.AddText("x286 y157 w560 h42 c526579", "")
window.AddText("x286 y207 w560 h18 c526579", "SYNTAX CARD")
syntaxBox := window.AddEdit("x286 y229 w560 h124 +Multi +ReadOnly -Wrap", "")
syntaxBox.SetFont("s10", "Monospace")
window.AddText("x286 y363 w560 h18 c526579", "RUNNABLE EXAMPLE")
exampleBox := window.AddEdit("x286 y385 w560 h180 +Multi +ReadOnly -Wrap", "")
exampleBox.SetFont("s10", "Monospace")
copyButton := window.AddButton("x686 y571 w160 h30", "Copy example")
copyButton.OnEvent("Click", CopyExample)

window.AddText("x868 y98 w322 h18 c526579", "SYNTAX CONCEPTS")
conceptTree := window.Add("TreeView", "x868 y120 w322 h214")
rootSyntax := conceptTree.Add("Core syntax")
conceptTree.Add("Assignment and expressions", rootSyntax)
conceptTree.Add("Control flow", rootSyntax)
conceptTree.Add("Functions and objects", rootSyntax)
rootAutomation := conceptTree.Add("Automation")
conceptTree.Add("Hotkeys and hotstrings", rootAutomation)
conceptTree.Add("GUI events", rootAutomation)
conceptTree.Add("Files and processes", rootAutomation)
conceptTree.OnEvent("ItemSelect", ConceptSelected)

window.AddText("x868 y352 w322 h18 c526579", "WHY IT MATTERS")
tipBox := window.AddEdit("x868 y374 w322 h95 +Multi +ReadOnly", "")
tipBox.SetFont("s10", "Sans")
window.AddText("x868 y485 w322 h18 c526579", "CHECKLIST")
checkBox := window.AddEdit("x868 y507 w322 h98 +Multi +ReadOnly -Wrap", "")
checkBox.SetFont("s10", "Sans")

window.AddText("x286 y610 w560 h18 c526579", "PRACTICE PAD")
practice := window.AddEdit("x286 y632 w560 h98 +Multi -Wrap", "")
practice.SetFont("s10", "Monospace")
runButton := window.AddButton("x868 y632 w98 h30", "Run")
runButton.OnEvent("Click", RunPractice)
checkButton := window.AddButton("x972 y632 w98 h30", "Check")
checkButton.OnEvent("Click", CheckPractice)
resetButton := window.AddButton("x1076 y632 w114 h30", "Reset")
resetButton.OnEvent("Click", ResetPractice)
previousButton := window.AddButton("x868 y670 w98 h30", "Previous")
previousButton.OnEvent("Click", PreviousLesson)
nextButton := window.AddButton("x972 y670 w98 h30", "Next")
nextButton.OnEvent("Click", NextLesson)
window.AddText("x1076 y670 w114 h30 c526579 Center", "Use F6 to run")

window.AddText("x18 y616 w250 h18 c526579", "ACTIVITY LOG")
activity := window.AddEdit("x18 y638 w250 h92 +Multi +ReadOnly -Wrap", "")
activity.SetFont("s9", "Monospace")
status := window.Add("StatusBar")
status.SetText("Ready | Choose a lesson to begin")
window.OnEvent("Close", (*) => ExitApp())

FilterLessons()
SetTimer(WatchLessonSelection, 50)
window.Show("w1220 h770")

FilterLessons(*) {
    global lessons, visibleIds, lessonList, search, level
    query := StrLower(Trim(search.Value))
    selectedLevel := level.Text
    visibleIds := []
    names := []
    for entry in lessons {
        if (selectedLevel != "All levels" && entry.level != selectedLevel)
            continue
        if (query != "" && !InStr(StrLower(entry.title " " entry.summary), query))
            continue
        visibleIds.Push(entry.id)
        names.Push(entry.id ". " entry.title)
    }
    lessonList.Delete()
    if names.Length
        lessonList.Add(names)
    else
        lessonList.Add(["No matching lessons"])
    if visibleIds.Length {
        lessonList.Value := 1
        LoadLesson()
    }
}

WatchLessonSelection(*) {
    global lessonList, lastLessonSelection
    selected := lessonList.Value
    if selected != lastLessonSelection {
        lastLessonSelection := selected
        LoadLessonNow()
    }
}

LoadLesson(*) {
    SetTimer(LoadLessonNow, -10)
}

LoadLessonNow(*) {
    global lessons, visibleIds, lessonList, lessonTitle, lessonMeta, summary, lastLessonSelection
    global syntaxBox, exampleBox, practice, tipBox, checkBox, progressText, status, activity
    if !visibleIds.Length
        return
    index := lessonList.Value
    if index < 1 || index > visibleIds.Length
        index := 1
    lastLessonSelection := index
    id := visibleIds[index]
    currentLesson := lessons[id]
    lessonTitle.Text := currentLesson.id ". " currentLesson.title
    lessonMeta.Text := currentLesson.level "  |  " currentLesson.checkWords.Length " key ideas  |  runnable pattern"
    summary.Text := currentLesson.summary
    syntaxBox.Value := currentLesson.syntax
    exampleBox.Value := currentLesson.example
    practice.Value := currentLesson.exercise
    tipBox.Value := currentLesson.tip
    checkBox.Value := "[ ] Read the syntax card`n[ ] Run the example`n[ ] Edit the practice pad`n[ ] Check your solution"
    progressText.Text := "Lesson " currentLesson.id " of " lessons.Length
    status.SetText("Ready | " currentLesson.title)
    activity.Value := "lesson=" currentLesson.id " loaded`nlevel=" currentLesson.level "`nstatus=ready"
}

CopyExample(*) {
    global exampleBox, status, activity
    A_Clipboard := exampleBox.Value
    status.SetText("Copied | Runnable example is now in the clipboard")
    activity.Value := "action=copy-example`nchars=" StrLen(exampleBox.Value) "`nstatus=complete"
}

RunPractice(*) {
    global practice, status, activity
    temp := A_Temp "/ahk-syntax-studio-practice.ahk"
    try FileDelete(temp)
    FileAppend("#Requires AutoHotkey v2.0`n" practice.Value "`nSetTimer(() => ExitApp(), -1500)`n", temp)
    try {
        command := Chr(34) A_AhkPath Chr(34) " " Chr(34) temp Chr(34)
        code := RunWait(command, , "Hide")
        status.SetText("Run complete | Exit code " code)
        activity.Value := "action=run`nexit_code=" code "`nstatus=complete"
    } catch as err {
        status.SetText("Run failed | " err.Message)
        activity.Value := "action=run`nstatus=error`nmessage=" err.Message
    }
}

CheckPractice(*) {
    global lessons, visibleIds, lessonList, practice, status, activity
    id := visibleIds[lessonList.Value]
    checkLesson := lessons[id]
    missing := []
    for word in checkLesson.checkWords
        if !InStr(practice.Value, word)
            missing.Push(word)
    if missing.Length {
        status.SetText("Needs another pass | Missing " missing.Length " key ideas")
        activity.Value := "action=check`nresult=keep-learning`nmissing=" JoinArray(missing, ", ")
    } else {
        status.SetText("Practice check passed | Keep experimenting")
        activity.Value := "action=check`nresult=pass`nideas=" checkLesson.checkWords.Length
    }
}

ResetPractice(*) {
    global lessons, visibleIds, lessonList, practice, status, activity
    id := visibleIds[lessonList.Value]
    practice.Value := lessons[id].exercise
    status.SetText("Practice reset | Starting pattern restored")
    activity.Value := "action=reset`nstatus=ready"
}

PreviousLesson(*) {
    global lessonList
    if lessonList.Value > 1
        lessonList.Value -= 1
    LoadLesson()
}

NextLesson(*) {
    global lessonList, visibleIds
    if lessonList.Value < visibleIds.Length
        lessonList.Value += 1
    LoadLesson()
}

ConceptSelected(*) {
    global status
    status.SetText("Concept selected | Use the lesson card to practice it")
}

JoinArray(items, separator) {
    result := ""
    for index, value in items
        result .= (index > 1 ? separator : "") value
    return result
}
