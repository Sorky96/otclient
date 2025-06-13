local TrackedTask = {
  widgets = {},
  offsetY = 80,
  spacing = 50,
  startX = 1200,
  startY = 0
}

function TrackedTask.init()

end

function TrackedTask.terminate()
  for _, data in pairs(TrackedTask.widgets) do
    if data.widget then data.widget:destroy() end
  end
  TrackedTask.widgets = {}
end

function TrackedTask.setTrackedTask(task)
  local existing = TrackedTask.widgets[task.name]
  if existing then
    
    if not existing.widget:isVisible() then
	  local yPosition = adjustYPosition()
      existing.widget:show()
      existing.widget:setPosition({ x = TrackedTask.startX, y = yPosition })
	  TrackedTask.startY = yPosition
    end
    
    return
  end

  local widget = g_ui.loadUI('tracked_task.otui', rootWidget)
  if not widget then
    print("[TrackedTask] Failed to create HUD")
    return
  end

  widget:setText(task.name)
  widget:show()
  local yPosition = adjustYPosition()
  widget:setPosition({ x = TrackedTask.startX, y = TrackedTask.startY + TrackedTask.offsetY })
  TrackedTask.startY = yPosition
  TrackedTask.widgets[task.name] = {
    widget = widget,
    task = task
  }
  
  widget.onDestroy = function()
    TrackedTask.widgets[task.name] = nil -- remove from tracked list
    if Tasks and Tasks.getSelectedTask and Tasks.getSelectedTask() == task and toggleTrackerButton then
      toggleTrackerButton:setOn(false)
      toggleTrackerButton:setText("Show Tracker")
    end
  end
  
  widget.onVisibilityChange = function(w, visible)
    if not visible then
      TrackedTask.widgets[task.name] = nil -- treat hidden as removed
      if Tasks and Tasks.getSelectedTask and Tasks.getSelectedTask() == task and toggleTrackerButton then
        toggleTrackerButton:setOn(false)
        toggleTrackerButton:setText("Show Tracker")
      end
    end
  end
  
  TrackedTask.updateSingle(task.name)
end

function TrackedTask.removeTrackedTask(task)
  local existing = TrackedTask.widgets[task.name]
  
  if existing then
    TrackedTask.startY = TrackedTask.startY - 80
    existing.widget:destroy()
	TrackedTask.widgets[task.name] = nil 
  end
end

function TrackedTask.updateSingle(taskName)
  local entry = TrackedTask.widgets[taskName]
  if not entry then return end

  local widget = entry.widget
  local task = entry.task
  widget:show()

  local contentBox = widget:getChildById('contentBox')
  local creature = contentBox and contentBox:getChildById('trackedCreature')
  if creature and task.lookType and task.lookType > 0 then
    creature:setOutfit({ type = task.lookType })
  else
    print("[TrackedTask] creature widget not found or invalid lookType")
  end

  local bar = contentBox and contentBox:getChildById('trackedProgress')
  if bar then
    local current = task.progress or 0
    local total = task.total or 1
    bar:setMinimum(0)
    bar:setMaximum(total)
    bar:setValue(current)
    bar:setText(string.format("%d / %d", current, total))
  else
    print("[TrackedTask] progress bar not found")
  end
end

function TrackedTask.tryUpdateFromList(taskList)
  for _, task in ipairs(taskList) do
    local tracked = TrackedTask.widgets[task.name]
    if tracked then
      tracked.task.progress = task.progress
      tracked.task.total = task.total
      TrackedTask.updateSingle(task.name)
    end
  end
end

function getTrackedTaskModule()
  return TrackedTask
end

function adjustYPosition()
  if TrackedTask.startY < 400 then
    return TrackedTask.startY + TrackedTask.offsetY
  else
    TrackedTask.startY = 0
    return 0
  end
end

function Tasks.updateTrackerToggleState()
  if tracked_task and tracked_task.widget and toggleTrackerButton then
    local visible = tracked_task.widget:isVisible()
    toggleTrackerButton:setOn(visible)
    toggleTrackerButton:setText(visible and "Hide Tracker" or "Show Tracker")
  end
end

function TrackedTask.anyVisible()
  for _, data in pairs(TrackedTask.widgets) do
    if data.widget and data.widget:isVisible() then
      return true
    end
  end
  return false
end

function TrackedTask.isTracked(task)
  return task and task.name and TrackedTask.widgets[task.name] ~= nil
end

_G.tracked_task = TrackedTask
