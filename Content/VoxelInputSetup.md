# C++ Voxel Tools Setup Instructions

## ✅ **What I've Created for You**

I've created a complete C++ voxel tool system with these files:

1. **VoxelToolComponent.h/.cpp** - Handles all voxel digging/building logic
2. **Updated ClaudeTestCharacter** - Third person character with voxel tools
3. **Build system updated** - Added CustomVoxel dependency

## 🎮 **Input Actions You Need to Set Up**

In the Unreal Editor, you need to create these Input Actions:

### **Step 1: Create Input Actions**

1. **Content Browser → Right-click → Input → Input Action**
2. **Create these 4 Input Actions:**
   - `IA_Dig` 
   - `IA_Build`
   - `IA_IncreaseToolSize`
   - `IA_DecreaseToolSize`

### **Step 2: Update Input Mapping Context**

1. **Open your existing Input Mapping Context** (probably `IMC_Default`)
2. **Add these mappings:**

```
IA_Dig → Left Mouse Button
IA_Build → Right Mouse Button  
IA_IncreaseToolSize → Mouse Wheel Up
IA_DecreaseToolSize → Mouse Wheel Down
```

### **Step 3: Configure Character Blueprint**

1. **Open BP_ThirdPersonCharacter**
2. **In the Details panel, find the Input section:**
   - Set **Dig Action** to `IA_Dig`
   - Set **Build Action** to `IA_Build` 
   - Set **Increase Tool Size Action** to `IA_IncreaseToolSize`
   - Set **Decrease Tool Size Action** to `IA_DecreaseToolSize`

## 🌍 **Voxel World Setup**

### **Step 1: Add Voxel World to Level**

1. **Place Actors → Search "Voxel World"**
2. **Drag into level at (0, 0, 0)**

### **Step 2: Configure Voxel World**

**Essential Settings:**
- **Render Type**: **Marching Cubes** ✓
- **Voxel Size**: 100
- **World Size**: 4 chunks
- **Enable Render**: ✓
- **Enable Collisions**: ✓

**Generator:**
- **Create new Data Asset → Voxel Generator → Flat Generator**
- **Assign to Voxel World**

**Material:**
- **Material Config**: RGB
- **Create basic material collection**

## 🎯 **How to Use**

Once set up, your controls will be:

- **Left Mouse Button**: Dig holes in voxel terrain
- **Right Mouse Button**: Build/add voxel material
- **Mouse Wheel Up**: Increase tool size
- **Mouse Wheel Down**: Decrease tool size

## 🔍 **Debug Features**

The C++ code includes:

- **Visual feedback**: Red spheres show where you're digging/building
- **On-screen messages**: Tool size changes display on screen
- **Console logging**: Check Output Log for debug info

## 🚨 **Troubleshooting**

### **If tools don't work:**

1. **Check Console for errors** - Look for "VoxelToolComponent" messages
2. **Verify Voxel World exists** - Should log "Found Voxel World: WorldName"
3. **Check Input Actions** - Make sure they're assigned in character blueprint
4. **Verify collision** - Make sure Voxel World has collision enabled

### **If no voxel terrain visible:**

1. **Check Render Type** - Must be "Marching Cubes"
2. **Check Generator** - Must have a valid generator assigned
3. **Check World Size** - Try increasing chunk count
4. **Check Camera position** - Make sure you're not underground

## 💡 **Advanced Usage**

The VoxelToolComponent exposes these Blueprint-callable functions:

- `DigAtLocation(Vector)` - Dig at specific world location
- `BuildAtLocation(Vector)` - Build at specific world location  
- `IncreaseToolSize()` - Increase tool radius
- `DecreaseToolSize()` - Decrease tool radius

You can call these from Blueprints for custom tools or UI buttons!

## 🔧 **Tool Settings**

In the VoxelToolComponent, you can adjust:

- **Tool Radius**: Size of digging/building sphere (50-1000)
- **Tool Strength**: How much material to add/remove (0.1-2.0)
- **Max Trace Distance**: How far to trace from camera (500-2000)

These are exposed in the Blueprint Details panel for easy tweaking!