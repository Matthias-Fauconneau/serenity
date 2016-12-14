#pragma once
#include "io/JsonSerializable.h"
#include "io/DirectoryChange.h"
#include "io/JsonObject.h"
#include "io/FileUtils.h"

struct Scene;

class RendererSettings : public JsonSerializable
{
    Path _outputDirectory;
    Path _outputFile;
    Path _hdrOutputFile;
    Path _varianceOutputFile;
    Path _resumeRenderFile;
    bool _overwriteOutputFiles;
    bool _useAdaptiveSampling;
    bool _enableResumeRender;
    bool _useSceneBvh;
    uint32 _spp;
    uint32 _sppStep;
    std::string _checkpointInterval;
    std::string _timeout;

public:
    RendererSettings()
    : _outputFile("TungstenRender.png"),
      _resumeRenderFile("TungstenRenderState.dat"),
      _overwriteOutputFiles(true),
      _useAdaptiveSampling(true),
      _enableResumeRender(false),
      _useSceneBvh(true),
      _spp(1),
      _sppStep(1),
      _checkpointInterval("0"),
      _timeout("0")
    {
    }

    virtual void fromJson(const rapidjson::Value &v, const Scene&)
    {
        ::fromJson(v, "output_directory", _outputDirectory);

        _outputDirectory.freezeWorkingDirectory();
        DirectoryChange change(_outputDirectory);

        ::fromJson(v, "output_file", _outputFile);
        ::fromJson(v, "hdr_output_file", _hdrOutputFile);
        ::fromJson(v, "variance_output_file", _varianceOutputFile);
        ::fromJson(v, "resume_render_file", _resumeRenderFile);
        ::fromJson(v, "overwrite_output_files", _overwriteOutputFiles);
        ::fromJson(v, "adaptive_sampling", _useAdaptiveSampling);
        ::fromJson(v, "enable_resume_render", _enableResumeRender);
        ::fromJson(v, "scene_bvh", _useSceneBvh);
        ::fromJson(v, "spp", _spp);
        ::fromJson(v, "spp_step", _sppStep);
        ::fromJson(v, "checkpoint_interval", _checkpointInterval);
        ::fromJson(v, "timeout", _timeout);
    }

    const Path &outputDirectory() const
    {
        return _outputDirectory;
    }

    void setOutputDirectory(const Path &directory)
    {
        _outputDirectory = directory;

        _outputFile        .setWorkingDirectory(_outputDirectory);
        _hdrOutputFile     .setWorkingDirectory(_outputDirectory);
        _varianceOutputFile.setWorkingDirectory(_outputDirectory);
        _resumeRenderFile  .setWorkingDirectory(_outputDirectory);
    }

    const Path &outputFile() const
    {
        return _outputFile;
    }

    void setOutputFile(const Path &file)
    {
        _outputFile = file;
    }

    const Path &hdrOutputFile() const
    {
        return _hdrOutputFile;
    }

    void setHdrOutputFile(const Path &file)
    {
        _hdrOutputFile = file;
    }

    const Path &varianceOutputFile() const
    {
        return _varianceOutputFile;
    }

    const Path &resumeRenderFile() const
    {
        return _resumeRenderFile;
    }

    bool overwriteOutputFiles() const
    {
        return _overwriteOutputFiles;
    }

    bool useAdaptiveSampling() const
    {
        return _useAdaptiveSampling;
    }

    bool enableResumeRender() const
    {
        return _enableResumeRender;
    }

    bool useSceneBvh() const
    {
        return _useSceneBvh;
    }

    uint32 spp() const
    {
        return _spp;
    }

    uint32 sppStep() const
    {
        return _sppStep;
    }

    std::string checkpointInterval() const
    {
        return _checkpointInterval;
    }

    std::string timeout() const
    {
        return _timeout;
    }

    void setUseSceneBvh(bool value)
    {
        _useSceneBvh = value;
    }

    void setSpp(uint32 spp)
    {
        _spp = spp;
    }

    void setSppStep(uint32 step)
    {
        _sppStep = step;
    }
};
