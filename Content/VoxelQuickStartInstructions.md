# Voxel Quick Start Guide - Marching Cubes Digging and Building

## Step 1: Setup Your Level

1. **Create New Level:**
   - File → New Level → Basic Level
   - Save as "VoxelTestLevel"

2. **Add Voxel World:**
   - Place Actors Panel → Search "Voxel World"
   - Drag into scene at location (0, 0, 0)

## Step 2: Configure Voxel World for Marching Cubes

Select your Voxel World and set these properties:

### **Generator Settings:**
- **Generator**: Create new "Flat Generator" or use existing example
- **Generator Instance**: Leave as default

### **Material Settings:**
- **Material Collection**: Use `/CustomVoxelSystem/Content/Examples/Shared/VoxelExamples_SimpleColorMaterial`
- **Voxel Material**: Set to "RGB" mode for simple colored voxels

### **Rendering Settings (IMPORTANT):**
- **Render Type**: **Marching Cubes** ← This is key!
- **Enable Render**: ✓ Checked
- **Remove Empty Chunks**: ✓ Checked
- **Material Config**: RGB

### **World Settings:**
- **Voxel Size**: **100** (good balance of detail vs performance)
- **World Size in Chunks**: **4x4x4** for testing
- **Max LOD**: **0** (highest detail)
- **Collision LOD**: **0**

### **Physics:**
- **Enable Collisions**: ✓ Checked
- **Max Collision LOD**: **0**

## Step 3: Add Player Controller with Tools

### Option A: Use Existing Simple Controller

1. **Set Game Mode:**
   - World Settings → Game Mode Override → VoxelSimpleGameMode (from plugin examples)
   - This includes basic movement and voxel tools

2. **Player Start:**
   - Add a Player Start actor above the voxel world
   - Position at (0, 0, 500) so you spawn above the terrain

### Option B: Create Custom Player Controller

1. **Create Player Character:**
   - Create new Blueprint based on "Character"
   - Name it "VoxelPlayerCharacter"

2. **Add Voxel Tools Component:**
   - Add "Voxel Tool Manager" component
   - Configure tool settings in the component

## Step 4: Configure Input for Digging/Building

### **Basic Tool Controls:**
- **Left Mouse Button**: Dig/Remove voxels
- **Right Mouse Button**: Build/Add voxels  
- **Mouse Wheel**: Change tool size
- **1-9 Keys**: Switch between different tools

### **Tool Types Available:**
- **Sphere Tool**: Round digging/building
- **Flatten Tool**: Create flat surfaces
- **Surface Tool**: Paint on existing surfaces
- **Smooth Tool**: Smooth rough edges
- **Trim Tool**: Remove specific materials

## Step 5: Example World Generator Setup

Create a simple flat world with some hills:

1. **Create Data Asset:**
   - Right-click in Content Browser
   - Miscellaneous → Data Asset
   - Choose "Voxel Generator"
   - Name it "MyFlatGenerator"

2. **Configure Generator:**
   - Set to generate a flat ground at height -500
   - Add some noise for gentle hills
   - Set density above ground to 0 (air) and below to 1 (solid)

## Step 6: Test Digging and Building

1. **Play the Level:**
   - Click Play button
   - You should spawn above a flat voxel world

2. **Test Tools:**
   - **Left Click**: Dig holes in the terrain
   - **Right Click**: Add voxel material
   - **Mouse Wheel**: Adjust tool size
   - Walk around and see marching cubes smoothly blend the voxel modifications

## Marching Cubes Benefits:

- **Smooth Surfaces**: No blocky/pixelated look like cubic voxels
- **Natural Terrain**: Hills, caves, and overhangs look realistic
- **Smooth Digging**: Tools create natural-looking excavations
- **Performance**: Good balance of quality and speed

## Troubleshooting:

- **No Voxels Visible**: Check that Generator is set and Render Type is Marching Cubes
- **Blocky Look**: Make sure you're using Marching Cubes, not Cubic rendering
- **Performance Issues**: Increase Voxel Size (200-500) or reduce World Size
- **No Collision**: Enable collision in Voxel World settings

## Next Steps:

- Experiment with different world generators
- Create custom materials for different voxel types
- Add multiplayer support for collaborative building
- Import external meshes as voxel data