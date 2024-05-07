// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause


/*
  Plugin example demonstrating colourspace handling
 */
#include <algorithm>
#include <stdexcept>
#include <new>
#include <string>
#include <array>
#include <cstring>
#include "spdlog/spdlog.h"
#define cimg_display 0          // no X11
#include "CImg.h"
#include "ofxImageEffect.h"
#include "ofxMemory.h"
#include "ofxMultiThread.h"
#include "ofxColour.h"
#include "ofxColourspaceList.h"

#include "../include/ofxUtilities.H" // example support utils

#if defined __APPLE__ || defined __linux__ || defined __FreeBSD__
#  define EXPORT __attribute__((visibility("default")))
#elif defined _WIN32
#  define EXPORT OfxExport
#else
#  error Not building on your operating system quite yet
#endif

const char *errMsg(int err) {
  switch (err) {
  case kOfxStatOK: return "OK";
  case kOfxStatFailed : return "Failed";
  case kOfxStatErrFatal: return "ErrFatal";
  case kOfxStatErrUnknown: return "ErrUnknown";
  case kOfxStatErrMissingHostFeature: return "ErrMissingHostFeature";
  case kOfxStatErrUnsupported: return "ErrUnsupported";
  case kOfxStatErrExists : return "ErrExists ";
  case kOfxStatErrFormat: return "ErrFormat";
  case kOfxStatErrMemory : return "ErrMemory";
  case kOfxStatErrBadHandle: return "ErrBadHandle";
  case kOfxStatErrBadIndex: return "ErrBadIndex";
  case kOfxStatErrValue: return "ErrValue";
  case kOfxStatReplyYes: return "ReplyYes";
  case kOfxStatReplyNo: return "ReplyNo";
  case kOfxStatReplyDefault: return "ReplyDefault";
  default: return "Unknown!?";
  }
}

// pointers to various bits of the host
OfxHost                 *gHost;
OfxImageEffectSuiteV1 *gEffectHost = 0;
OfxPropertySuiteV1    *gPropHost = 0;
OfxParameterSuiteV1   *gParamHost = 0;
OfxMemorySuiteV1      *gMemoryHost = 0;
OfxMultiThreadSuiteV1 *gThreadHost = 0;
OfxMessageSuiteV1     *gMessageSuite = 0;
OfxInteractSuiteV1    *gInteractHost = 0;

// some flags about the host's behaviour
int gHostSupportsMultipleBitDepths = false;
std::string gHostColourManagementStyle;

// private instance data type
struct MyInstanceData {
  bool isGeneralEffect;

  // handles to the clips we deal with
  OfxImageClipHandle sourceClip;
  OfxImageClipHandle outputClip;

  // handles to a our parameters
  OfxParamHandle scaleParam;
  OfxParamHandle perComponentScaleParam;
  OfxParamHandle scaleRParam;
  OfxParamHandle scaleGParam;
  OfxParamHandle scaleBParam;
  OfxParamHandle scaleAParam;
};

/* mandatory function to set up the host structures */


// Convenience wrapper to get private data 
static MyInstanceData *
getMyInstanceData( OfxImageEffectHandle effect)
{
  // get the property handle for the plugin
  OfxPropertySetHandle effectProps;
  gEffectHost->getPropertySet(effect, &effectProps);

  // get my data pointer out of that
  MyInstanceData *myData = 0;
  gPropHost->propGetPointer(effectProps,  kOfxPropInstanceData, 0, 
			    (void **) &myData);
  return myData;
}

// Convenience wrapper to set the enabledness of a parameter
static inline void
setParamEnabledness( OfxImageEffectHandle effect,
                    const char *paramName,
                    int enabledState)
{
  // fetch the parameter set for this effect
  OfxParamSetHandle paramSet;
  gEffectHost->getParamSet(effect, &paramSet);
  
  // fetch the parameter property handle
  OfxParamHandle param; OfxPropertySetHandle paramProps;
  gParamHost->paramGetHandle(paramSet, paramName, &param, &paramProps);

  // and set its enabledness
  gPropHost->propSetInt(paramProps,  kOfxParamPropEnabled, 0, enabledState);
}

// function that sets the enabledness of the percomponent scale parameters
// depending on the value of the 
// This function is called when the 'scaleComponents' value is changed
// or when the input clip has been changed
static void
setPerComponentScaleEnabledness( OfxImageEffectHandle effect)
{
  // get my instance data
  MyInstanceData *myData = getMyInstanceData(effect);

  // get the value of the percomponent scale param
  int perComponentScale;
  gParamHost->paramGetValue(myData->perComponentScaleParam, &perComponentScale);

  if(ofxuIsClipConnected(effect, kOfxImageEffectSimpleSourceClipName)) {
    OfxPropertySetHandle props; gEffectHost->clipGetPropertySet(myData->sourceClip, &props);

    // get the input clip format
    char *pixelType;
    gPropHost->propGetString(props, kOfxImageEffectPropComponents, 0, &pixelType);

    // only enable the scales if the input is an RGBA input
    perComponentScale = perComponentScale && !(strcmp(pixelType, kOfxImageComponentAlpha) == 0);
  }

  // set the enabled/disabled state of the parameter
  setParamEnabledness(effect, "scaleR", perComponentScale);
  setParamEnabledness(effect, "scaleG", perComponentScale);
  setParamEnabledness(effect, "scaleB", perComponentScale);
  setParamEnabledness(effect, "scaleA", perComponentScale);
}

/** @brief Called at load */
static OfxStatus
onLoad(void)
{
  return kOfxStatOK;
}

/** @brief Called before unload */
static OfxStatus
onUnLoad(void)
{
  return kOfxStatOK;
}

//  instance construction
static OfxStatus
createInstance( OfxImageEffectHandle effect)
{
  // get a pointer to the effect properties
  OfxPropertySetHandle effectProps;
  gEffectHost->getPropertySet(effect, &effectProps);

  // get a pointer to the effect's parameter set
  OfxParamSetHandle paramSet;
  gEffectHost->getParamSet(effect, &paramSet);

  // make my private instance data
  MyInstanceData *myData = new MyInstanceData;
  char *context = 0;

  // is this instance a general effect ?
  gPropHost->propGetString(effectProps, kOfxImageEffectPropContext, 0,  &context);
  myData->isGeneralEffect = context && (strcmp(context, kOfxImageEffectContextGeneral) == 0);

  // cache param handles
  gParamHost->paramGetHandle(paramSet, "scaleComponents", &myData->perComponentScaleParam, 0);
  gParamHost->paramGetHandle(paramSet, "scale", &myData->scaleParam, 0);
  gParamHost->paramGetHandle(paramSet, "scaleR", &myData->scaleRParam, 0);
  gParamHost->paramGetHandle(paramSet, "scaleG", &myData->scaleGParam, 0);
  gParamHost->paramGetHandle(paramSet, "scaleB", &myData->scaleBParam, 0);
  gParamHost->paramGetHandle(paramSet, "scaleA", &myData->scaleAParam, 0);

  // cache clip handles
  gEffectHost->clipGetHandle(effect, kOfxImageEffectSimpleSourceClipName, &myData->sourceClip, 0);
  gEffectHost->clipGetHandle(effect, kOfxImageEffectOutputClipName, &myData->outputClip, 0);
  
  // set my private instance data
  gPropHost->propSetPointer(effectProps, kOfxPropInstanceData, 0, (void *) myData);

  // As the parameters values have already been loaded, set 
  // the enabledness of the per component scale values
  setPerComponentScaleEnabledness(effect);

  return kOfxStatOK;
}

// instance destruction
static OfxStatus
destroyInstance( OfxImageEffectHandle  effect)
{
  // get my instance data
  MyInstanceData *myData = getMyInstanceData(effect);

  // and delete it
  if(myData)
    delete myData;
  return kOfxStatOK;
}

// tells the host what region we are capable of filling
OfxStatus 
getSpatialRoD( OfxImageEffectHandle  effect,  OfxPropertySetHandle inArgs,  OfxPropertySetHandle outArgs)
{
  // retrieve any instance data associated with this effect
  MyInstanceData *myData = getMyInstanceData(effect);

  OfxTime time;
  gPropHost->propGetDouble(inArgs, kOfxPropTime, 0, &time);
  
  // my RoD is the same as my input's
  OfxRectD rod;
  gEffectHost->clipGetRegionOfDefinition(myData->sourceClip, time, &rod);

  // set the rod in the out args
  gPropHost->propSetDoubleN(outArgs, kOfxImageEffectPropRegionOfDefinition, 4, &rod.x1);

  return kOfxStatOK;
}

// tells the host how much of the input we need to fill the given window
OfxStatus 
getSpatialRoI( OfxImageEffectHandle  effect,  OfxPropertySetHandle inArgs,  OfxPropertySetHandle outArgs)
{
  // get the RoI the effect is interested in from inArgs
  OfxRectD roi;
  gPropHost->propGetDoubleN(inArgs, kOfxImageEffectPropRegionOfInterest, 4, &roi.x1);

  // the input needed is the same as the output, so set that on the source clip
  gPropHost->propSetDoubleN(outArgs, "OfxImageClipPropRoI_Source", 4, &roi.x1);

  // retrieve any instance data associated with this effect
  MyInstanceData *myData = getMyInstanceData(effect);

  return kOfxStatOK;
}

// Tells the host how many frames we can fill. Only called in the general context.
// This shows the default behaviour; shown for illustrative purposes.
OfxStatus 
getTemporalDomain( OfxImageEffectHandle  effect,  OfxPropertySetHandle /*inArgs*/,  OfxPropertySetHandle outArgs)
{
  MyInstanceData *myData = getMyInstanceData(effect);

  double sourceRange[2];
  
  // get the frame range of the source clip
  OfxPropertySetHandle props; gEffectHost->clipGetPropertySet(myData->sourceClip, &props);
  gPropHost->propGetDoubleN(props, kOfxImageEffectPropFrameRange, 2, sourceRange);

  // set it on the out args
  gPropHost->propSetDoubleN(outArgs, kOfxImageEffectPropFrameRange, 2, sourceRange);
  
  return kOfxStatOK;
}


// Set our clip preferences 
static OfxStatus 
getClipPreferences( OfxImageEffectHandle  effect,  OfxPropertySetHandle /*inArgs*/,  OfxPropertySetHandle outArgs)
{
  // retrieve any instance data associated with this effect
  MyInstanceData *myData = getMyInstanceData(effect);
  
  // get the component type and bit depth of our main input
  int  bitDepth;
  bool isRGBA;
  ofxuClipGetFormat(myData->sourceClip, bitDepth, isRGBA, true); // get the unmapped clip component

  // get the strings used to label the various bit depths
  const char *bitDepthStr = bitDepth == 8 ? kOfxBitDepthByte : (bitDepth == 16 ? kOfxBitDepthShort : kOfxBitDepthFloat);
  const char *componentStr = isRGBA ? kOfxImageComponentRGBA : kOfxImageComponentAlpha;

  // set out output to be the same same as the input, component and bitdepth
  gPropHost->propSetString(outArgs, "OfxImageClipPropComponents_Output", 0, componentStr);
  if(gHostSupportsMultipleBitDepths)
    gPropHost->propSetString(outArgs, "OfxImageClipPropDepth_Output", 0, bitDepthStr);

  // Colour management -- preferred colour spaces, in order (most preferred first)
#define PREFER_COLOURSPACES
#ifdef PREFER_COLOURSPACES
  if (gHostColourManagementStyle != kOfxImageEffectPropColourManagementNone) {
    spdlog::info("Specifying preferred colourspaces since host style={}", gHostColourManagementStyle);
    const char* colourSpaces[] = {
      kOfxColourspaceACEScg,
      kOfxColourspaceLinRec2020,
      kOfxColourspaceRoleSceneLinear};
    for (int i = 0; i < std::size(colourSpaces); i++) {
      spdlog::info("Specifying preferred colourspace {} = {}", i, colourSpaces[i]);
      gPropHost->propSetString(outArgs, kOfxImageClipPropPreferredColourspaces,
                               i, colourSpaces[i]);
    }
  } else {
    spdlog::info("Host does not support colour management (this example won't be very interesting)");
  }
#endif

  return kOfxStatOK;
}

// are the settings of the effect performing an identity operation
static OfxStatus
isIdentity( OfxImageEffectHandle  effect,
	    OfxPropertySetHandle inArgs,
	    OfxPropertySetHandle outArgs)
{
  return kOfxStatReplyDefault;  // this example is never identity; always render
}

////////////////////////////////////////////////////////////////////////////////
// function called when the instance has been changed by anything
static OfxStatus
instanceChanged( OfxImageEffectHandle  effect,
		 OfxPropertySetHandle inArgs,
		 OfxPropertySetHandle /*outArgs*/)
{
  // see why it changed
  char *changeReason;
  gPropHost->propGetString(inArgs, kOfxPropChangeReason, 0, &changeReason);

  // we are only interested in user edits
  if(strcmp(changeReason, kOfxChangeUserEdited) != 0) return kOfxStatReplyDefault;

  // fetch the type of the object that changed
  char *typeChanged;
  gPropHost->propGetString(inArgs, kOfxPropType, 0, &typeChanged);

  // was it a clip or a param?
  bool isClip = strcmp(typeChanged, kOfxTypeClip) == 0;
  bool isParam = strcmp(typeChanged, kOfxTypeParameter) == 0;

  // get the name of the thing that changed
  char *objChanged;
  gPropHost->propGetString(inArgs, kOfxPropName, 0, &objChanged);

  // Did the source clip change or the 'scaleComponents' change? In which case enable/disable individual component scale parameters
  if((isClip && strcmp(objChanged, kOfxImageEffectSimpleSourceClipName)  == 0) ||
     (isParam && strcmp(objChanged, "scaleComponents")  == 0)) {
    setPerComponentScaleEnabledness(effect);
    return kOfxStatOK;
  }

  // don't trap any others
  return kOfxStatReplyDefault;
}


static std::string getClipColourspace(const OfxImageClipHandle clip) {
    OfxPropertySetHandle clipProps;
    gEffectHost->clipGetPropertySet(clip, &clipProps);
    char *tmpStr = NULL;
    OfxStatus status = gPropHost->propGetString(clipProps, kOfxImageClipPropColourspace, 0, &tmpStr);
    if (status == kOfxStatOK) {
      return std::string(tmpStr);
    } else {
      spdlog::info("Can't get clip's colourspace; propGetString returned {}", errMsg(status));
      return std::string("unspecified");
    }

}

// the process code  that the host sees
static OfxStatus render( OfxImageEffectHandle  instance,
                         OfxPropertySetHandle inArgs,
                         OfxPropertySetHandle /*outArgs*/)
{
  // get the render window and the time from the inArgs
  OfxTime time;
  OfxRectI renderWindow;
  OfxStatus status = kOfxStatOK;

  gPropHost->propGetDouble(inArgs, kOfxPropTime, 0, &time);
  gPropHost->propGetIntN(inArgs, kOfxImageEffectPropRenderWindow, 4, &renderWindow.x1);

  // retrieve any instance data associated with this effect
  MyInstanceData *myData = getMyInstanceData(instance);

  // property handles and members of each image
  // in reality, we would put this in a struct as the C++ support layer does
  OfxPropertySetHandle sourceImg = NULL, outputImg = NULL, maskImg = NULL;
  int srcRowBytes, srcBitDepth, dstRowBytes, dstBitDepth, maskRowBytes = 0, maskBitDepth;
  bool srcIsAlpha, dstIsAlpha, maskIsAlpha = false;
  OfxRectI dstRect, srcRect, maskRect = {0, 0, 0, 0};
  void *src, *dst, *mask = NULL;

  std::string inputColourspace = getClipColourspace(myData->sourceClip);
  spdlog::info("source clip colourspace = {}", inputColourspace);
  std::string outputColourspace = getClipColourspace(myData->outputClip);
  spdlog::info("output clip colourspace = {}", outputColourspace);

  try {
    // get the source image
    sourceImg = ofxuGetImage(myData->sourceClip, time, srcRowBytes, srcBitDepth, srcIsAlpha, srcRect, src);
    if(sourceImg == NULL) throw OfxuNoImageException();

    // get the output image
    outputImg = ofxuGetImage(myData->outputClip, time, dstRowBytes, dstBitDepth, dstIsAlpha, dstRect, dst);
    if(outputImg == NULL) throw OfxuNoImageException();

    // see if they have the same depths and bytes and all
    if(srcBitDepth != dstBitDepth || srcIsAlpha != dstIsAlpha || srcRowBytes != dstRowBytes) {
      throw OfxuStatusException(kOfxStatErrImageFormat);
    }

    int xdim = srcRect.x2 - srcRect.x1;
    int ydim = srcRect.y2 - srcRect.y1;
    // do the rendering

    int nchannels = 4;
    int font_height = 128;
    spdlog::info("Rendering {}x{} image @{},{}, depth={}", xdim, ydim, srcRect.x1, srcRect.y1, dstBitDepth);
    switch(dstBitDepth) {
    case 8 : {
      using T = unsigned char;
      auto img = cimg_library::CImg<T>((const T *)src, xdim, ydim, 1, nchannels, true);
      T fg[] = {255, 255, 0, 255};
      T bg[] = {0, 0, 0, 0};
      img.draw_text(100, 100, "Test (byte)!", fg, bg, 1.0, font_height);
      memcpy(dst, src, srcRowBytes * ydim * nchannels);
    }
      break;
    case 16 : {
      using T = unsigned short;
      auto img = cimg_library::CImg<T>((const T *)src, xdim, ydim, 1, nchannels, true);
      T fg[] = {65535, 65535, 0, 65535};
      T bg[] = {0, 0, 0, 0};
      img.draw_text(100, 100, "Test (short)!", fg, bg, 1.0, font_height);
      memcpy(dst, src, srcRowBytes * ydim * nchannels);
    }
      break;
    case 32 : {
      using T = float;
      auto img = cimg_library::CImg<T>((const T *)src, xdim, ydim, 1, nchannels, true);
      T fg[] = {1.0, 0.0, 1.0, 1.0};
      T bg[] = {0, 0, 0, 0};
      img.mirror('y');
      img.draw_text(100, 100, "Test (float)!", fg, bg, 1.0, font_height);
      img.mirror('y');
      memcpy(dst, src, srcRowBytes * ydim * nchannels);
    }
      break;
    }
  }
  catch(OfxuNoImageException &ex) {
    // if we were interrupted, the failed fetch is fine, just return kOfxStatOK
    // otherwise, something weird happened
    if(!gEffectHost->abort(instance)) {
      status = kOfxStatFailed;
    }
  }
  catch(OfxuStatusException &ex) {
    status = ex.status();
  }

  // release the data pointers
  if(sourceImg)
    gEffectHost->clipReleaseImage(sourceImg);
  if(outputImg)
    gEffectHost->clipReleaseImage(outputImg);
  
  return status;
}


//  describe the plugin in context
static OfxStatus
describeInContext( OfxImageEffectHandle  effect,  OfxPropertySetHandle inArgs)
{
  // get the context from the inArgs handle
  char *context;
  gPropHost->propGetString(inArgs, kOfxImageEffectPropContext, 0, &context);
  bool isGeneralContext = strcmp(context, kOfxImageEffectContextGeneral) == 0;

  OfxPropertySetHandle props;
  // define the single output clip in both contexts
  gEffectHost->clipDefine(effect, kOfxImageEffectOutputClipName, &props);

  // set the component types we can handle on out output
  gPropHost->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);

  // define the single source clip in both contexts
  gEffectHost->clipDefine(effect, kOfxImageEffectSimpleSourceClipName, &props);

  // set the component types we can handle on our main input
  gPropHost->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);

  ////////////////////////////////////////////////////////////////////////////////
  // define the parameters for this context
  // fetch the parameter set from the effect
  OfxParamSetHandle paramSet;
  gEffectHost->getParamSet(effect, &paramSet);

  return kOfxStatOK;
}

////////////////////////////////////////////////////////////////////////////////
// the plugin's description routine
static OfxStatus
describe(OfxImageEffectHandle  effect)
{
  // first fetch the host APIs, this cannot be done before this call
  OfxStatus stat;
  if((stat = ofxuFetchHostSuites()) != kOfxStatOK)
    return stat;

  // record a few host features
  gPropHost->propGetInt(gHost->host, kOfxImageEffectPropSupportsMultipleClipDepths, 0, &gHostSupportsMultipleBitDepths);
  char *tmpStr = NULL;
  stat = gPropHost->propGetString(
                                  gHost->host, kOfxImageEffectPropColourManagementStyle, 0, &tmpStr);
  if (stat == kOfxStatOK) {
    gHostColourManagementStyle = tmpStr;
    spdlog::info("describe: host says its colour management style is '{}'", gHostColourManagementStyle);
  } else {
    spdlog::info("describe: host does not support colour management (err={})", errMsg(stat));
    gHostColourManagementStyle = kOfxImageEffectPropColourManagementNone;
  }

  // get the property handle for the plugin
  OfxPropertySetHandle effectProps;
  gEffectHost->getPropertySet(effect, &effectProps);

  // We can render both fields in a fielded images in one hit if there is no animation
  // So set the flag that allows us to do this
  gPropHost->propSetInt(effectProps, kOfxImageEffectPluginPropFieldRenderTwiceAlways, 0, 0);

  // say we can support multiple pixel depths and let the clip preferences action deal with it all.
  gPropHost->propSetInt(effectProps, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 1);
  
  // set the bit depths the plugin can handle
  gPropHost->propSetString(effectProps, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthByte);
  gPropHost->propSetString(effectProps, kOfxImageEffectPropSupportedPixelDepths, 1, kOfxBitDepthShort);
  gPropHost->propSetString(effectProps, kOfxImageEffectPropSupportedPixelDepths, 2, kOfxBitDepthFloat);

  // set some labels and the group it belongs to
  gPropHost->propSetString(effectProps, kOfxPropLabel, 0, "OFX Colourspace Example");
  gPropHost->propSetString(effectProps, kOfxImageEffectPluginPropGrouping, 0, "OFX Example");

  // define the contexts we can be used in
  gPropHost->propSetString(effectProps, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);
  gPropHost->propSetString(effectProps, kOfxImageEffectPropSupportedContexts, 1, kOfxImageEffectContextGeneral);

  if (gHostColourManagementStyle != kOfxImageEffectPropColourManagementNone) {
    // host supports colour management, either OCIO or Native.
    // OCIO implies native, so here we can assume it supports native.
    // Tell it we support native.
    stat = gPropHost->propSetString(effectProps,
                                    kOfxImageEffectPropColourManagementStyle, 0,
                                    kOfxImageEffectPropColourManagementNative);
    if (stat != kOfxStatOK) {
      spdlog::error("setting kOfxImageEffectPropColourManagementStyle prop: stat={}", errMsg(stat));
    }
  }

  return kOfxStatOK;
}

////////////////////////////////////////////////////////////////////////////////
// The main function
static OfxStatus
pluginMain(const char *action,  const void *handle, OfxPropertySetHandle inArgs,  OfxPropertySetHandle outArgs)
{
  OfxStatus stat = kOfxStatOK;

  spdlog::info(">>> pluginMain({})", action);
  try {
  // cast to appropriate type
  OfxImageEffectHandle effect = (OfxImageEffectHandle) handle;

  if(strcmp(action, kOfxActionDescribe) == 0) {
    stat = describe(effect);
  }
  else if(strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) {
    stat = describeInContext(effect, inArgs);
  }
  else if(strcmp(action, kOfxActionLoad) == 0) {
    stat = onLoad();
  }
  else if(strcmp(action, kOfxActionUnload) == 0) {
    stat = onUnLoad();
  }
  else if(strcmp(action, kOfxActionCreateInstance) == 0) {
    stat = createInstance(effect);
  } 
  else if(strcmp(action, kOfxActionDestroyInstance) == 0) {
    stat = destroyInstance(effect);
  } 
  else if(strcmp(action, kOfxImageEffectActionIsIdentity) == 0) {
    stat = isIdentity(effect, inArgs, outArgs);
  }    
  else if(strcmp(action, kOfxImageEffectActionRender) == 0) {
    stat = render(effect, inArgs, outArgs);
  }    
  else if(strcmp(action, kOfxImageEffectActionGetRegionOfDefinition) == 0) {
    stat = getSpatialRoD(effect, inArgs, outArgs);
  }  
  else if(strcmp(action, kOfxImageEffectActionGetRegionsOfInterest) == 0) {
    stat = getSpatialRoI(effect, inArgs, outArgs);
  }  
  else if(strcmp(action, kOfxImageEffectActionGetClipPreferences) == 0) {
    stat = getClipPreferences(effect, inArgs, outArgs);
  }  
  else if(strcmp(action, kOfxActionInstanceChanged) == 0) {
    stat = instanceChanged(effect, inArgs, outArgs);
  }  
  else if(strcmp(action, kOfxImageEffectActionGetTimeDomain) == 0) {
    stat = getTemporalDomain(effect, inArgs, outArgs);
  }  
  } catch (std::bad_alloc) {
    // catch memory
    spdlog::error("Caught OFX Plugin Memory error");
    stat = kOfxStatErrMemory;
  } catch ( const std::exception& e ) {
    // standard exceptions
    spdlog::error("Caught OFX Plugin error {}", e.what());
    stat = kOfxStatErrUnknown;
  } catch (int err) {
    spdlog::error("Caught misc error {}", err);
    stat = err;
  } catch ( ... ) {
    // everything else
    spdlog::error("Caught unknown OFX plugin error");
    stat = kOfxStatErrUnknown;
  }
  // other actions to take the default value


  spdlog::info("<<< pluginMain({}) = {}", action, errMsg(stat));
  return stat;
}

// function to set the host structure
static void
setHostFunc(OfxHost *hostStruct)
{
  gHost         = hostStruct;
}

////////////////////////////////////////////////////////////////////////////////
// the plugin struct 
static OfxPlugin colourspacePlugin =
{       
  kOfxImageEffectPluginApi,
  1,
  "io.aswf.openfx.example.ColourspacePlugin",
  1,
  0,
  setHostFunc,
  pluginMain
};
   
// the two mandated functions
EXPORT OfxPlugin *
OfxGetPlugin(int nth)
{
  if(nth == 0)
    return &colourspacePlugin;
  return 0;
}
 
EXPORT int
OfxGetNumberOfPlugins(void)
{       
  return 1;
}

// Called first after loading. This is optional for plugins.
EXPORT OfxStatus
OfxSetHost()
{
  return kOfxStatOK;
}

struct SharedLibResource {
  SharedLibResource() {
    spdlog::set_level(spdlog::level::trace);
    spdlog::trace("shlib/dll loaded");
  }
  ~SharedLibResource() {
  }
};
static SharedLibResource _sharedLibResource;
