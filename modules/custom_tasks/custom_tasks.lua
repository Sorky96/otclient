Tasks = {}
local selectedTask
local suppressCheckChange = false
local DEBOUNCE_DELAY_MS = 100
local pendingSelectedEntry = nil
local refreshScheduled = false
local scheduledEvents = {}
local currentSearchText = ''
local searchDebounceEvent = nil
local lastTrackedTask = nil
local hideCompleted = false
local toggleTrackerButton = nil


function init()
  g_ui.importStyle('custom_tasks_styles.otui')
  g_ui.importStyle('reward_item.otui')
  g_ui.importStyle('monster_widget.otui')
  Tasks.window = g_ui.displayUI('custom_tasks.otui')
  Tasks.window:hide()
  if g_game.isOnline() and not Tasks.list then
    g_game.getProtocolGame():sendExtendedOpcode(125)
  end

  g_keyboard.bindKeyDown('Up', selectPreviousTask)
  g_keyboard.bindKeyDown('Down', selectNextTask)
  g_keyboard.bindKeyDown('Ctrl+D', toggle)

  taskButton = modules.game_mainpanel.addToggleButton('taskButton', tr('Tasks') .. ' (Ctrl+D)',
                                                      '/images/options/ButtonBosstiary',
                                                      toggle, false, 99)
  ProtocolGame.registerExtendedOpcode(125, onTaskOpcode)
  toggleTrackerButton = Tasks.window:recursiveGetChildById('toggleTrackerButton')

  local searchBox = Tasks.window:recursiveGetChildById('taskSearchBox')
  if searchBox then
    searchBox.onTextChange = function(widget, text)
      currentSearchText = text:lower()
      if searchDebounceEvent then removeEvent(searchDebounceEvent) end
      searchDebounceEvent = scheduleEvent(function()
        refreshTaskList()
        searchDebounceEvent = nil
      end, 250)
    end
  end

  local hideButton = Tasks.window:recursiveGetChildById('hideCompletedButton')
  if hideButton then
    hideButton.onClick = function()
      hideCompleted = not hideCompleted
      hideButton:setText(hideCompleted and "Show Completed" or "Hide Completed")
      refreshTaskList()
    end
  end

  toggleTrackerButton.onClick = function()
    if not selectedTask or not selectedTask.taskData then return end
    local task = selectedTask.taskData
  
    if tracked_task.isTracked(task) then
      Tasks.tryDestroyTrackedTask(task)
    else
      Tasks.trySetTrackedTask(task)
    end
  end  
  
  local function checkTrackedWidgetState()
    if toggleTrackerButton and selectedTask and selectedTask.taskData then
      local task = selectedTask.taskData
      local isTracked = tracked_task.isTracked(task)
      local widgetEntry = tracked_task.widgets[task.name]
      local isVisible = isTracked and widgetEntry and widgetEntry.widget:isVisible()
  
      toggleTrackerButton:setOn(isVisible)
      toggleTrackerButton:setText(isVisible and "Hide Tracker" or "Show Tracker")
    end
  
    addEvent(checkTrackedWidgetState)
  end
  
  addEvent(checkTrackedWidgetState)


end

function terminate()
  if Tasks.window then Tasks.window:destroy() end
  g_keyboard.unbindKeyDown('Up', selectPreviousTask)
  g_keyboard.unbindKeyDown('Down', selectNextTask)
  g_keyboard.unbindKeyDown('Ctrl+D')
  ProtocolGame.unregisterExtendedOpcode(125)
end

function toggle()
  if g_game.isOnline() and (not Tasks.list or #Tasks.list == 0) then
    g_game.getProtocolGame():sendExtendedOpcode(125)
  end
  if not Tasks.window then return end

  if taskButton:isOn() then
    Tasks.window:hide()
    taskButton:setOn(false)
  else
    refreshTaskList()

    if not Tasks.window:getParent() then
      local panel = modules.game_interface.findContentPanelAvailable(Tasks.window, Tasks.window:getMinimumHeight())
      if not panel then return end
      panel:addChild(Tasks.window)
    end

    Tasks.window:show()
    Tasks.window:raise()
    Tasks.window:focus()
    taskButton:setOn(true)
  end
end

function refreshTaskList()
  suppressCheckChange = true

  local flatPanel = Tasks.window:getChildById('flatPanel')
  if not flatPanel then return end

  local panel = flatPanel:getChildById('taskPanel')
  if not panel then return end

  local scrollBar = flatPanel:getChildById('textlistScrollBar')
  local scrollValue = scrollBar and scrollBar:getValue() or 0

  local previouslySelectedTaskName = selectedTask and selectedTask.taskData and selectedTask.taskData.name

  panel:destroyChildren()

  local entryToSelect = nil
  local searchBox = Tasks.window:recursiveGetChildById('taskSearchBox')
  local searchFilter = searchBox and searchBox:getText():lower() or ''

  local visibleIndex = 0

  for index, task in ipairs(Tasks.list or {}) do
  if searchFilter == '' or task.name:lower():find(searchFilter, 1, true) then
    local isCompleted = task.progress and task.total and task.progress >= task.total
    if hideCompleted and isCompleted then
      goto continue
    end

    local entry = g_ui.createWidget('TaskListItem', panel)
    entry.taskData = task

    if visibleIndex == 0 then
      entry:setMarginTop(26)
    end
    visibleIndex = visibleIndex + 1

    if isCompleted then
      entry:setOn("completed", true)
    end

    entry.onClick = function()
      if selectedEntry == entry then return end
      if selectedEntry then
        selectedEntry:setChecked(false)
      end
      entry:setChecked(true)
      selectedEntry = entry
      handleTaskSelection(entry)
    end

    if previouslySelectedTaskName and previouslySelectedTaskName == task.name then
      entryToSelect = entry
    elseif not entryToSelect then
      entryToSelect = entry
    end

    local label = entry:getChildById('taskLabel')
    if label then
      label:setText(task.name)
      label.onDoubleClick = function()
        Tasks.trySetTrackedTask(entry.taskData)
      end
    end

    local creatureUI = entry:getChildById('creatureUI')
    if creatureUI then
      creatureUI:setOutfit({ type = task.lookType })
    end
  end
  ::continue::
end


  suppressCheckChange = false

  if entryToSelect then
    entryToSelect:setChecked(true)
    handleTaskSelection(entryToSelect)
  end

  if scrollBar then
    scrollBar:setValue(scrollValue)
  end
end

function onTaskOpcode(protocol, opcode, buffer)
  local ok, data = pcall(function() return json.decode(buffer) end)
  if not ok or not data then
    print("[TASK OPCODE] Failed to decode JSON:", buffer)
    return
  end

  Tasks.list = Tasks.list or {}

  local taskList = type(data[1]) == "table" and data or { data }

  for _, task in ipairs(taskList) do
    local found = false
    for _, t in ipairs(Tasks.list) do	
      if t.name == task.name then
        t.progress = task.progress
        t.total = task.total
        t.rewards = task.rewards or {}
        t.monster = task.monster or {}
        t.lookType = task.lookType
        found = true
        break
      end
    end
    if not found then
      table.insert(Tasks.list, task)
    end
  end

  if tracked_task then
    tracked_task.tryUpdateFromList(taskList)
  end

  if not refreshScheduled then
    refreshScheduled = true
    scheduleEvent(function()
      if Tasks.window and Tasks.window:isVisible() then
        refreshTaskList()
      end
      refreshScheduled = false
    end, 100)
  end
end

function getSelectedTask()
  local taskPanel = Tasks.window:getChildById('taskPanel')
  if not taskPanel then return nil end

  for i = 1, taskPanel:getChildCount() do
    local widget = taskPanel:getChildByIndex(i)
    if widget:isChecked() then
      return widget.taskData
    end
  end

  return nil
end

function handleTaskSelection(widget)
  if not widget or widget:isDestroyed() then return end
  if selectedTask and selectedTask == widget then return end
  if selectedTask and not selectedTask:isDestroyed() and selectedTask ~= widget then
    selectedTask:setChecked(false)
  end

  widget:setChecked(true)
  selectedTask = widget
  selectedEntry = widget

  local task = widget.taskData
  if not task then return end

  local infoPanel = Tasks.window:getChildById('infoPanel')
  if not infoPanel then return end

  local progressBar = infoPanel:getChildById('taskProgress')
  if progressBar and task.total and task.progress then
    progressBar:setMinimum(0)
    progressBar:setMaximum(task.total)
    progressBar:setValue(task.progress)
    progressBar:setText(string.format("%d / %d", task.progress, task.total))
  end

  local rewardPanel = infoPanel:getChildById('rewardPanel')
  if rewardPanel then
    rewardPanel:destroyChildren()
    for _, reward in ipairs(task.rewards or {}) do
      local icon = g_ui.createWidget('RewardItemWidget', rewardPanel)
      if icon then
        icon:setItemId(tonumber(reward.itemId or 0))
        icon:setTooltip("x" .. tostring(reward.count or '?'))
        icon:resize(32, 32)
        if reward.count > 1 then icon:setText("x" .. tostring(reward.count or '?')) end
      end
    end
  end

  local monsterPanel = infoPanel:getChildById('monsterPanel')
  if monsterPanel then
    monsterPanel:destroyChildren()
    local monsters = type(task.monster) == "table" and task.monster or {
      { name = task.monster, lookType = task.lookType, points = 1 }
    }

    for _, monster in ipairs(monsters) do
      local monsterIcon = g_ui.createWidget('MonsterUI', monsterPanel)
      if monsterIcon then
        monsterIcon:setOutfit({ type = monster.lookType })
        monsterIcon:setTooltip(monster.name)
        monsterIcon:setText("x" .. tostring(monster.points or '?'))
        monsterIcon:resize(32, 32)
      end
    end
  end

  if toggleTrackerButton and tracked_task and selectedTask and selectedTask.taskData then
    local isTracked = tracked_task.isTracked(selectedTask.taskData)
    toggleTrackerButton:setOn(isTracked)
    toggleTrackerButton:setText(isTracked and "Hide Tracker" or "Show Tracker")
  end
end

function debounceSendTaskSelection(entry)
  pendingSelectedEntry = entry

  if debounceEvent then
    removeEvent(debounceEvent)
  end

  debounceEvent = scheduleEvent(function()
    if pendingSelectedEntry and pendingSelectedEntry:isChecked() then
      uncheckAllTasks(pendingSelectedEntry)
      handleTaskSelection(pendingSelectedEntry)
    end
    debounceEvent = nil
    pendingSelectedEntry = nil
  end, DEBOUNCE_DELAY_MS)
end

function uncheckAllTasks(except)
  local flatPanel = Tasks.window:getChildById('flatPanel')
  if not flatPanel then return end

  local taskPanel = flatPanel:getChildById('taskPanel')
  if not taskPanel then return end

  for _, child in pairs(taskPanel:getChildren()) do
    if child ~= except then
      child:setChecked(false)
    end
  end
end

function selectPreviousTask()
  debounceEvent("task_nav", 100, function()
  if not selectedTask or not selectedTask:getParent() or not Tasks.window or not Tasks.window:isFocused() then return end


    local flatPanel = Tasks.window:getChildById('flatPanel')
    local panel = flatPanel:getChildById('taskPanel')
    local scrollBar = flatPanel:getChildById('textlistScrollBar')
    if not panel or not scrollBar then return end

    local children = panel:getChildren()
    local step = scrollBar:getStep() or 1

    for i = #children, 1, -1 do
      if children[i] == selectedTask and i > 1 then
        local newSelected = children[i - 1]
        handleTaskSelection(newSelected)
        scrollBar:setValue((scrollBar:getValue() - step) + 5 )
        break
      end
    end
  end)
end

function selectNextTask()
  debounceEvent("task_nav", 100, function()
    if not selectedTask or not selectedTask:getParent() or not Tasks.window or not Tasks.window:isFocused() then return end


    local flatPanel = Tasks.window:getChildById('flatPanel')
    local panel = flatPanel:getChildById('taskPanel')
    local scrollBar = flatPanel:getChildById('textlistScrollBar')
    if not panel or not scrollBar then return end

    local children = panel:getChildren()
    local step = scrollBar:getStep() or 1

    for i = 1, #children do
      if children[i] == selectedTask and i < #children then
        local newSelected = children[i + 1]
        handleTaskSelection(newSelected)
        scrollBar:setValue((scrollBar:getValue() + step) - 5)
        break
      end
    end
  end)
end

function debounceEvent(id, delay, fn)
  if scheduledEvents[id] then
    removeEvent(scheduledEvents[id])
  end
  scheduledEvents[id] = scheduleEvent(function()
    scheduledEvents[id] = nil
    fn()
  end, delay)
end

local function trySetTrackedTask(task)
  if tracked_task and tracked_task.setTrackedTask then
    if not tracked_task.isTracked(task) then
		tracked_task.setTrackedTask(task)
	end
    lastTrackedTask = task

	if toggleTrackerButton and tracked_task and tracked_task.anyVisible then
	local visible = tracked_task.anyVisible()
	toggleTrackerButton:setOn(visible)
	toggleTrackerButton:setText(visible and "Hide Tracker" or "Show Tracker")
	end

  end
end

local function tryDestroyTrackedTask(task)
  if tracked_task and tracked_task.removeTrackedTask then
    tracked_task.removeTrackedTask(task)
    lastTrackedTask = nil

    if toggleTrackerButton then
      toggleTrackerButton:setOn(false)
      toggleTrackerButton:setText("Show Tracker")
    end
  end
end

function table.find(t, value)
  for i, v in ipairs(t) do
    if v == value then return i end
  end
end

Tasks.trySetTrackedTask = trySetTrackedTask
Tasks.tryDestroyTrackedTask = tryDestroyTrackedTask

_G.Tasks = Tasks
return Tasks