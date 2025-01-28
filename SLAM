Objectives:
- Create a visual SLAM system for an ESP32-CAM robot
- Minimize computational requirements
- Maintain reliable localization
- Use aesthetically acceptable markers in a garden environment
- Support up to 64 unique locations/tags

Operating Environments:
1. Indoor Room:
   - 12' x 12' (3.7m x 3.7m)
   - Controlled lighting
   - Fixed tag positions on walls/corners
2. Primary Target - Garden Space:
   - 40' x 40' (12.2m x 12.2m)
   - Outdoor lighting conditions

3x3 Tag Design:
- 9-cell grid (3x3) plus border pattern
- 6 data bits + 3 error correction bits = 64 unique tags
- Implements Hamming(9,6) error correction code
  - Single-bit error correction capability
  - Double-bit error detection capability
- Minimum detection size: 12-15 pixels across full tag

6-inch tag detection ranges:
- Optimal: 6-10 feet (1.8-3m)
- Reliable: up to 18 feet (5.5m)
- Reduced but Usable: 18-22 feet (5.5-6.7m)
  - Single-bit errors automatically corrected
  - Double-bit errors detected and rejected
  - Multi-bit errors discarded

12-inch tag detection ranges:
- Optimal: 12-20 feet (3.6-6.1m)
- Reliable: up to 36 feet (11m)
- Reduced but Usable: 36-44 feet (11-13.4m)
  - Same error correction capabilities as 6" tags

Tag Visibility Requirements:
- Minimum: 1 tag for basic position estimation
- Optimal: 2-3 tags for robust pose estimation
- More tags improve accuracy but not required
- Tag spacing should ensure overlap of detection zones

ESP32-CAM Parameters:
- Resolution: 1600x1200
- FOV: ~60° horizontal
- Processing time per frame: estimated 20-30ms
- Memory footprint: ~30-40KB for tag detection

Deployment Strategy:
1. Garden Perimeter:
   - 12-inch tags 
   - 6-8 tags for basic boundary definition
   - Tags placed to ensure reliable range overlap
2. Interior Space:
   - 6-inch tags
   - 8-12 tags for path/zone coverage
   - Spacing optimized for reliable detection range