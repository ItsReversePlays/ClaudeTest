# Simple Voxel Setup - No Blueprint Errors

## Step 1: Create Basic Level

1. **File → New Level → Basic Level**
2. **Save as "MyVoxelLevel"**

## Step 2: Add Voxel World Manually

1. **Place Actors Panel → Search "Voxel World"**
2. **Drag Voxel World into scene at (0, 0, 0)**

## Step 3: Configure Voxel World (No Blueprints)

Select the Voxel World and set:

### Generator:
- **Create new → Data Asset → Voxel Generator → Flat Generator**
- **Name it "MyFlatGenerator"**
- **Assign to Voxel World**

### Rendering:
- **Render Type**: Marching Cubes
- **Material Collection**: Create new basic material collection
- **Voxel Size**: 100
- **World Size**: 4

### Materials:
- **Material Config**: RGB
- **Create Basic Material**: Use Engine default materials

## Step 4: Use Default Character

Instead of voxel-specific characters:

1. **World Settings → Game Mode → Game Mode Base**
2. **Default Pawn Class**: ThirdPersonCharacter or FirstPersonCharacter
3. **Add Player Start** above the voxel world

## Step 5: Add Voxel Tools via C++/Blueprint

Create a simple **Actor Component** for tools:

1. **Right-click → Blueprint → Actor Component**
2. **Name: "SimpleVoxelTools"**
3. **Add to your character**

### Component Setup:
- **Add Input Events**: Left/Right click
- **Use Voxel Function Library calls directly**
- **No complex blueprint dependencies**

## Step 6: Basic Tool Implementation

In your component's Blueprint:

### Left Click (Dig):
```
Event: Left Mouse Button Pressed
→ Line Trace from Camera
→ Voxel Sphere Tool (Remove material)
→ Radius: 200
→ Strength: 1.0
```

### Right Click (Build):
```
Event: Right Mouse Button Pressed  
→ Line Trace from Camera
→ Voxel Sphere Tool (Add material)
→ Radius: 200
→ Strength: 1.0
```

## Alternative: Working Example

If you want to avoid blueprint issues entirely:

1. **Use the example maps that DON'T have errors**
2. **Create your own simple character**
3. **Use C++ Voxel Tool classes directly**

This approach avoids all the blueprint compilation issues while still giving you marching cubes voxel editing!