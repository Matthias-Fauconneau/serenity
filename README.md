# Rock documentation

## Usage
 Command line arguments are interpreted as targets, paths or arguments (can be interleaved in any order).
 The first path to a folder is the path to the folder containing the source data set image slices (shorthand form of the "path" argument)
 Targets can be any output result from a production rule of the process definition.
 Each target will be copied to the corresponding target path (i.e first target with first target path, ...).
 Arguments affects either the process definition or the operations themselves.

## Targets and arguments
- \ref Source "source"
  %Image slices from the "path" folder concatenated as a volume.
  - path
    Path to the slice images folder (or to a volume file)
    \note %Folder names of the form "name-resolution" can be used to automatically assign the correct input resolution (in nm).
              Otherwise the "resolution" argument will be used (default: 1μm)
  - downsample = [0]|1
    Downsamples the volume by two.
    \note Unlike "resample", the full resolution is never stored in memory as downsampling is done as the slices are loaded.
  - cylinder = x,y,r,z0,z1 | r,z | r
    Crops the volume to the cylinder of radius r centered at (x,y) including all slices of coordinate z with z0<=z<z1 (i.e excluding z1 slice for a total z1-z0 slices)
    \note Voxel coordinates starts from zero
  - box = x0,y0,z0,x1,y1,z1 | x,y,z | r
    Crops the volume to the box from x0,y0,z0 to x1,y1,z1
    \note Voxel coordinates starts from zero
  - extra
    Adds 2 voxels to the cylinder to compensate for margins lost to processing
  \note It can be useful to store the cropped/downsampled source volume on the filesystem when extracting/downsampling from a large original dataset takes a long time.
- name
  The name of the data set (i.e everything before any dash in the "path" folder name)
- resolution
  The resolution of the data set in μm (i.e number after the dash in the folder name divided by 1000)
  \note The "downsample" argument will properly double the resolution.
- voxelSize
  The number of voxels of the source volume in each dimension
- physicalSize
  The physical size of the source volume in each dimension
- denoised
  The denoised volume
  - denoise = 0|[median]
    Selects the denoising method. Median filtering is recommended. Downsampling should be used instead of average filter. No filter should be used on already clean samples.
- resampled
  The resampled volume
  - resample = [0]|1 (unused)
    Resamples the denoised volume from resolution to process-resolution
    - "process-resolution" must be given (in the same unit as "resolution" (e.g μm))
- histogram-radiodensity[-normalizedY]
  Histogram of the radiodensities (voxels versus radiodensity value)
  - thresholdFromSource = [0]|1 (unused)
    Selects whether to evaluate the histogram from the source or the denoised/resampled volume.
    \note The threshold will only be optimal if computed on the processed volume (default)
- distribution-radiodensity
  Estimates the underlying radiodensity probability density (using kernel density estimation on the histogram)
  \note Radiodensity is normalized to the 0-1 range and area is normalized to 1 (i.e correct probability density function)
- threshold
  The uniform radiodensity threshold used to segment pores
  - threshold = [otsu]|gradient|lorentz|<decimal>|<integer>
   The threshold determination method to use or a manual threshold (given either in normalized or integer radiodensity)
   \note Otsu is the recommended robust statistic method, gradient is for backward compatibility only, Lorentz is an explicit model which may be more accurate but is prone to fail.
   \note otsu-parameters, otsu-interclass-deviation[-normalized], gradient-mean, lorentz-parameters, lorentz-rock, lorentz-notrock, lorentz-pore, lorentz-notpore can be used for insight in the determination.
- transposed (internal)
   Transposed volume (to use an alternate cylinder axis)
  - transpose = X|Y|[Z]
     Input axis to map to the cylinder Z axis.
- rock (internal)
   Binary segmentation mask
- colored (visualization)
  Visualization of radiodensities colored by the binary segmentation (pore in green, rock in red)
- distance (internal)
  Distance field (squared distance from every pore voxel to the nearest rock voxel)
- skeleton (internal)
  Integer medial axis skeleton (squared radius of all largest spheres)
- pruned (internal)
  Skeleton without the smallest pores
  - minimalSqRadius = [0]|<integer>
   Sets every skeleton voxel with a squared radius less than or equal to minimalSqRadius to 0 (rock)
- crop (internal)
  Cropped processed volume to remove boundary bias effects (before floodfill to avoid hidden connections)
  - crop.cylinder = x,y,r,z0,z1 | r
   Specification of the cylinder to crop in the original source coordinate (i.e before source crop (not this input))
- connected (internal)
 Skeleton without unconnected pores.
 - connect-pore = 0|[1]
  Selects whether to discard unconnected pores.
- histogram-radius[-scaled]
 Histogram of the pore radii (voxels versus radius)
 \note This histogram is the square root of the histogram of squared distance (which is integer as voxel coordinates are integer).
           The original histogram contains all integers r². Since √''<0, bins gets closer as r² increases.
           As bins are not uniformly spaced anymore, the result cannot be interpreted as a discrete probability density
 \note histogram-radius-scaled has the X axis scaled from voxels to μm (using "resolution")
- volume-distribution-radius[-scaled]
 Pore size distribution estimated from radius histogram using kernel density estimation
 \note The result is unnormalized (i.e not a PDF (probability density function)) to compare with the histogram
- distribution-radius[-scaled]
 Pore size distribution normalized by the pore volume (excluding rock (not a PDF (sums to porosity (and not 1))))
 - distribution-radius[-scaled]-normalized
  Pore size distribution normalized by the total volume (including rock (is a PDF (sums to 1)))
- volume
  %Volume of the connected pore space in voxels
- volume-total
  Total volume of the discrete cylinder in voxels
- porosity
  Relative volume of the pore space (i.e pore space volume over total volume)
- mean-radius
  Mean radius of the pores (in voxels)
- cdl-maximum
  Volume of the rounded maximum radii exported in CDL format (to be converted to a netCDF file using ncgen)
- ascii
  Volume of the rounded maximum radii exported in ASCII format (one sample per line formatted as "x, y, z, r")
- png-{source,denoised,colorize,distance,skeleton,maximum}
  %Image slices of the volume exported in PNG format (normalized and gamma-compressed for visualization)
- bmp-{source,denoised,distance,skeleton,maximum}
  %Image slices of the volume exported in BMP format (unnormalized and linear for interoperation)
- [bmp-]denoised-connected
  Denoised volume with all unconnected pores set to "value"
  - value = <decimal>|<integer> [mandatory]
   Radiodensity value to set masked voxels
- [bmp-]pore-not-flood
  Volume with voxels assigned 255 for unconnected/unflooded pore space, or 0 otherwise (rock and connected/flooded pore space).
  - [bmp-]pore-not-flood
    Volume with voxels assigned 255 for unconnected/unflooded pore space, or 0 otherwise (rock and connected/flooded pore space).

## Tools:
- Representative elementary volume:
 Computes the pore size distributions on 8 cylinders of varying radius R centered on the octants of the source volume
 - PSD(R)
  Pore size distributions across the 8 cylinders for each cylinder radius R
 - PSD(octant|R:first) PSD(octant|R:inflection) PSD(octant|R:last)
  Pore size distribution of each octant for the first, inflection and last R
 - ε(R) ε(R|r<median) ε(R|r>median)
  Deviation between the octants for each R estimated on the full distribution, the lower half or the higher half.
 - representative-radius
  Cylinder radius at the inflection point assumed to be large enough to estimate properties of the full rock (in voxels).
- Largest bottleneck radius:
 Computes the largest pruning radius which allows at least one connection
 - unconnected(λ) connected(λ)
  Volume of the [un]connected pore space versus pruning radius λ
 - critical-radius
  Critical pruning radius which allows at least one connection (in voxels)
- Analysis summary:
 Presents all important informations extracted from a data set by this tool
 - slices
  Slide containing:
   - Source header identifying data set by name, resolution, voxel size, physical size
   - Slices of the source, denoising, segmentation, distance field, skeleton, pore size volumes
 - plots
  Slide containing:
   - Plots of the threshold determination, pore size distribution, REV deviation and largest bottleneck radius estimation
   - Property footer with porosity,
 - summary
  A4 page containing source header, slices, plots and property footer.
- Comparator:
 Compares results of enabling a given parameter when generating a given target
 - compare
  Volume showing blue where A<B, red where A>B or the original value where A=B
 - target
  Target volume to compare
 - parameter
  Parameter to toggle
 - A = 0, B = 1
  Value of the parameter for each case

## Visualization
 - view
  Enables visualization. By default, all given targets are presented in a window.
 - slides
  Presents each target in a different window.
 - png
  Writes PNG images of the visualization into the home folder.
 - pdf
  Writes a PDF with one image per page into the home folder.

## Examples
    Rock binary: /pool/users/mfauconneau/rock.fast
    Berea dataset: /pool/5G/rock_physics/validation/test_cases/drpbm_berea/ct_scan/tif.org
    Command line: /pool/users/mfauconneau/rock.fast view summary /pool/5G/rock_physics/validation/test_cases/drpbm_berea/ct_scan/tif.org

## Developer information
 Rock is written using [C++11][http://en.wikipedia.org/wiki/C++11]

### Configuring local development environment without a package manager
Install the scripts
 cp scripts/* /ptmp/bin

Fetch the package index
 index

Build and install gcc dependencies
 build gmp-5.1.2
 build mpfr-3.1.2
 build mpc-1.0.1

Build and install gcc
 build gcc-4.8.1 --disable-multilib --enable-languages="c,c++" --with-mpc=/ptmp

Add /ptmp/{bin,lib,include} to your system paths
 cat env.txt >> ~/.bashrc
Content of env.txt
 export PATH=/ptmp/bin:$PATH
 export LD_LIBRARY_PATH=/ptmp/lib:$LD_LIBRARY_PATH
 export CPPFLAGS=-I/ptmp/include
 export LDFLAGS=-L/ptmp/lib

Build and install rock tool
 cd /pool/users/mfauconneau/rock
 sh build.sh fast rock /ptmp/bin

Optionnal: Build and install gdb (debugger)
 build gdb-7.6

Optionnal: Build and install Qt and Qt Creator (IDE)
 build-qt
 build qt-creator-2.8.0
 You can now open serenity.creator with Qt Creator
 The code can be browsed using F2 to follow symbols (Alt-Left to go back)

### Creating a new operator
    To create a new operator, copy an existing operator, closest to your goal, to a new implementation file (.cc).
    The build system automatically compile implementation file whenever the corresponding interface file (.h) is included.
    As all operators share the same Operation interface, an interface file is not required.
    The build system can still be configured to compile the implementation file using a commented include command (e.g //#include "median.h")
    Operation is the most abstract interface, VolumeOperation should be used for operations on volume, VolumePass can be used for single input, single output volume operations.
