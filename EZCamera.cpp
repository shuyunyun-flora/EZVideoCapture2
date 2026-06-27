#include "EZCamera.h"
#include <QCoreApplication>
#include <QDebug>
#include <QThread>
#include <QWidget>
#include <QTimer>
#include <iostream>
#include <string>
#include <regex>
#include <algorithm>
#include "EZCameraDeviceErrorEvent.h"

#ifdef Q_OS_WIN
#include <mfapi.h>
#include <mfidl.h>
#include <Mferror.h>
#include <mfreadwrite.h>
#include <dshow.h>
#include <atlbase.h>
#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mf.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "strmiids.lib")

template <class T> void SafeRelease(T** ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = NULL;
	}
}

#include <strsafe.h>

LPCWSTR GetGUIDNameConst(const GUID& guid);
HRESULT GetGUIDName(const GUID& guid, WCHAR** ppwsz);

HRESULT LogAttributeValueByIndex(IMFAttributes* pAttr, DWORD index);
HRESULT SpecialCaseAttributeValue(GUID guid, const PROPVARIANT& var);

void DBGMSG(PCWSTR format, ...);

HRESULT LogMediaType(IMFMediaType* pType)
{
	UINT32 count = 0;

	HRESULT hr = pType->GetCount(&count);
	if (FAILED(hr))
	{
		return hr;
	}

	if (count == 0)
	{
		DBGMSG(L"Empty media type.\n");
	}

	for (UINT32 i = 0; i < count; i++)
	{
		hr = LogAttributeValueByIndex(pType, i);
		if (FAILED(hr))
		{
			break;
		}
	}
	return hr;
}

HRESULT LogAttributeValueByIndex(IMFAttributes* pAttr, DWORD index)
{
	WCHAR* pGuidName = NULL;
	WCHAR* pGuidValName = NULL;

	GUID guid = { 0 };

	PROPVARIANT var;
	PropVariantInit(&var);

	HRESULT hr = pAttr->GetItemByIndex(index, &guid, &var);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = GetGUIDName(guid, &pGuidName);
	if (FAILED(hr))
	{
		goto done;
	}

	DBGMSG(L"\t%s\t", pGuidName);

	hr = SpecialCaseAttributeValue(guid, var);
	if (FAILED(hr))
	{
		goto done;
	}
	if (hr == S_FALSE)
	{
		switch (var.vt)
		{
		case VT_UI4:
			DBGMSG(L"%d", var.ulVal);
			break;

		case VT_UI8:
			DBGMSG(L"%I64d", var.uhVal);
			break;

		case VT_R8:
			DBGMSG(L"%f", var.dblVal);
			break;

		case VT_CLSID:
			hr = GetGUIDName(*var.puuid, &pGuidValName);
			if (SUCCEEDED(hr))
			{
				DBGMSG(pGuidValName);
			}
			break;

		case VT_LPWSTR:
			DBGMSG(var.pwszVal);
			break;

		case VT_VECTOR | VT_UI1:
			DBGMSG(L"<<byte array>>");
			break;

		case VT_UNKNOWN:
			DBGMSG(L"IUnknown");
			break;

		default:
			DBGMSG(L"Unexpected attribute type (vt = %d)", var.vt);
			break;
		}
	}

done:
	DBGMSG(L"\n");
	CoTaskMemFree(pGuidName);
	CoTaskMemFree(pGuidValName);
	PropVariantClear(&var);
	return hr;
}

HRESULT GetGUIDName(const GUID& guid, WCHAR** ppwsz)
{
	HRESULT hr = S_OK;
	WCHAR* pName = NULL;

	LPCWSTR pcwsz = GetGUIDNameConst(guid);
	if (pcwsz)
	{
		size_t cchLength = 0;

		hr = StringCchLength(pcwsz, STRSAFE_MAX_CCH, &cchLength);
		if (FAILED(hr))
		{
			goto done;
		}

		pName = (WCHAR*)CoTaskMemAlloc((cchLength + 1) * sizeof(WCHAR));

		if (pName == NULL)
		{
			hr = E_OUTOFMEMORY;
			goto done;
		}

		hr = StringCchCopy(pName, cchLength + 1, pcwsz);
		if (FAILED(hr))
		{
			goto done;
		}
	}
	else
	{
		hr = StringFromCLSID(guid, &pName);
	}

done:
	if (FAILED(hr))
	{
		*ppwsz = NULL;
		CoTaskMemFree(pName);
	}
	else
	{
		*ppwsz = pName;
	}
	return hr;
}

void LogUINT32AsUINT64(const PROPVARIANT& var)
{
	UINT32 uHigh = 0, uLow = 0;
	Unpack2UINT32AsUINT64(var.uhVal.QuadPart, &uHigh, &uLow);
	DBGMSG(L"%d x %d", uHigh, uLow);
}

float OffsetToFloat(const MFOffset& offset)
{
	return offset.value + (static_cast<float>(offset.fract) / 65536.0f);
}

HRESULT LogVideoArea(const PROPVARIANT& var)
{
	if (var.caub.cElems < sizeof(MFVideoArea))
	{
		return MF_E_BUFFERTOOSMALL;
	}

	MFVideoArea* pArea = (MFVideoArea*)var.caub.pElems;

	DBGMSG(L"(%f,%f) (%d,%d)", OffsetToFloat(pArea->OffsetX), OffsetToFloat(pArea->OffsetY),
		pArea->Area.cx, pArea->Area.cy);
	return S_OK;
}

// Handle certain known special cases.
HRESULT SpecialCaseAttributeValue(GUID guid, const PROPVARIANT& var)
{
	if ((guid == MF_MT_FRAME_RATE) || (guid == MF_MT_FRAME_RATE_RANGE_MAX) ||
		(guid == MF_MT_FRAME_RATE_RANGE_MIN) || (guid == MF_MT_FRAME_SIZE) ||
		(guid == MF_MT_PIXEL_ASPECT_RATIO))
	{
		// Attributes that contain two packed 32-bit values.
		LogUINT32AsUINT64(var);
	}
	else if ((guid == MF_MT_GEOMETRIC_APERTURE) ||
		(guid == MF_MT_MINIMUM_DISPLAY_APERTURE) ||
		(guid == MF_MT_PAN_SCAN_APERTURE))
	{
		// Attributes that an MFVideoArea structure.
		return LogVideoArea(var);
	}
	else
	{
		return S_FALSE;
	}
	return S_OK;
}

void DBGMSG(PCWSTR format, ...)
{
	va_list args;
	va_start(args, format);

	WCHAR msg[MAX_PATH];

	if (SUCCEEDED(StringCbVPrintf(msg, sizeof(msg), format, args)))
	{
		OutputDebugString(msg);
	}
}

#ifndef IF_EQUAL_RETURN
#define IF_EQUAL_RETURN(param, val) if(val == param) return L#val
#endif

LPCWSTR GetGUIDNameConst(const GUID& guid)
{
	IF_EQUAL_RETURN(guid, MF_MT_MAJOR_TYPE);
	IF_EQUAL_RETURN(guid, MF_MT_MAJOR_TYPE);
	IF_EQUAL_RETURN(guid, MF_MT_SUBTYPE);
	IF_EQUAL_RETURN(guid, MF_MT_ALL_SAMPLES_INDEPENDENT);
	IF_EQUAL_RETURN(guid, MF_MT_FIXED_SIZE_SAMPLES);
	IF_EQUAL_RETURN(guid, MF_MT_COMPRESSED);
	IF_EQUAL_RETURN(guid, MF_MT_SAMPLE_SIZE);
	IF_EQUAL_RETURN(guid, MF_MT_WRAPPED_TYPE);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_NUM_CHANNELS);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_SAMPLES_PER_SECOND);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_FLOAT_SAMPLES_PER_SECOND);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_AVG_BYTES_PER_SECOND);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_BLOCK_ALIGNMENT);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_BITS_PER_SAMPLE);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_VALID_BITS_PER_SAMPLE);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_SAMPLES_PER_BLOCK);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_CHANNEL_MASK);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_FOLDDOWN_MATRIX);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_PEAKREF);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_PEAKTARGET);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_AVGREF);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_AVGTARGET);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_PREFER_WAVEFORMATEX);
	IF_EQUAL_RETURN(guid, MF_MT_AAC_PAYLOAD_TYPE);
	IF_EQUAL_RETURN(guid, MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION);
	IF_EQUAL_RETURN(guid, MF_MT_FRAME_SIZE);
	IF_EQUAL_RETURN(guid, MF_MT_FRAME_RATE);
	IF_EQUAL_RETURN(guid, MF_MT_FRAME_RATE_RANGE_MAX);
	IF_EQUAL_RETURN(guid, MF_MT_FRAME_RATE_RANGE_MIN);
	IF_EQUAL_RETURN(guid, MF_MT_PIXEL_ASPECT_RATIO);
	IF_EQUAL_RETURN(guid, MF_MT_DRM_FLAGS);
	IF_EQUAL_RETURN(guid, MF_MT_PAD_CONTROL_FLAGS);
	IF_EQUAL_RETURN(guid, MF_MT_SOURCE_CONTENT_HINT);
	IF_EQUAL_RETURN(guid, MF_MT_VIDEO_CHROMA_SITING);
	IF_EQUAL_RETURN(guid, MF_MT_INTERLACE_MODE);
	IF_EQUAL_RETURN(guid, MF_MT_TRANSFER_FUNCTION);
	IF_EQUAL_RETURN(guid, MF_MT_VIDEO_PRIMARIES);
	IF_EQUAL_RETURN(guid, MF_MT_CUSTOM_VIDEO_PRIMARIES);
	IF_EQUAL_RETURN(guid, MF_MT_YUV_MATRIX);
	IF_EQUAL_RETURN(guid, MF_MT_VIDEO_LIGHTING);
	IF_EQUAL_RETURN(guid, MF_MT_VIDEO_NOMINAL_RANGE);
	IF_EQUAL_RETURN(guid, MF_MT_GEOMETRIC_APERTURE);
	IF_EQUAL_RETURN(guid, MF_MT_MINIMUM_DISPLAY_APERTURE);
	IF_EQUAL_RETURN(guid, MF_MT_PAN_SCAN_APERTURE);
	IF_EQUAL_RETURN(guid, MF_MT_PAN_SCAN_ENABLED);
	IF_EQUAL_RETURN(guid, MF_MT_AVG_BITRATE);
	IF_EQUAL_RETURN(guid, MF_MT_AVG_BIT_ERROR_RATE);
	IF_EQUAL_RETURN(guid, MF_MT_MAX_KEYFRAME_SPACING);
	IF_EQUAL_RETURN(guid, MF_MT_DEFAULT_STRIDE);
	IF_EQUAL_RETURN(guid, MF_MT_PALETTE);
	IF_EQUAL_RETURN(guid, MF_MT_USER_DATA);
	IF_EQUAL_RETURN(guid, MF_MT_AM_FORMAT_TYPE);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG_START_TIME_CODE);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG2_PROFILE);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG2_LEVEL);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG2_FLAGS);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG_SEQUENCE_HEADER);
	IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_SRC_PACK_0);
	IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_CTRL_PACK_0);
	IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_SRC_PACK_1);
	IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_CTRL_PACK_1);
	IF_EQUAL_RETURN(guid, MF_MT_DV_VAUX_SRC_PACK);
	IF_EQUAL_RETURN(guid, MF_MT_DV_VAUX_CTRL_PACK);
	IF_EQUAL_RETURN(guid, MF_MT_ARBITRARY_HEADER);
	IF_EQUAL_RETURN(guid, MF_MT_ARBITRARY_FORMAT);
	IF_EQUAL_RETURN(guid, MF_MT_IMAGE_LOSS_TOLERANT);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG4_SAMPLE_DESCRIPTION);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG4_CURRENT_SAMPLE_ENTRY);
	IF_EQUAL_RETURN(guid, MF_MT_ORIGINAL_4CC);
	IF_EQUAL_RETURN(guid, MF_MT_ORIGINAL_WAVE_FORMAT_TAG);

	// Media types

	IF_EQUAL_RETURN(guid, MFMediaType_Audio);
	IF_EQUAL_RETURN(guid, MFMediaType_Video);
	IF_EQUAL_RETURN(guid, MFMediaType_Protected);
	IF_EQUAL_RETURN(guid, MFMediaType_SAMI);
	IF_EQUAL_RETURN(guid, MFMediaType_Script);
	IF_EQUAL_RETURN(guid, MFMediaType_Image);
	IF_EQUAL_RETURN(guid, MFMediaType_HTML);
	IF_EQUAL_RETURN(guid, MFMediaType_Binary);
	IF_EQUAL_RETURN(guid, MFMediaType_FileTransfer);

	IF_EQUAL_RETURN(guid, MFVideoFormat_AI44); //     FCC('AI44')
	IF_EQUAL_RETURN(guid, MFVideoFormat_ARGB32); //   D3DFMT_A8R8G8B8 
	IF_EQUAL_RETURN(guid, MFVideoFormat_AYUV); //     FCC('AYUV')
	IF_EQUAL_RETURN(guid, MFVideoFormat_DV25); //     FCC('dv25')
	IF_EQUAL_RETURN(guid, MFVideoFormat_DV50); //     FCC('dv50')
	IF_EQUAL_RETURN(guid, MFVideoFormat_DVH1); //     FCC('dvh1')
	IF_EQUAL_RETURN(guid, MFVideoFormat_DVSD); //     FCC('dvsd')
	IF_EQUAL_RETURN(guid, MFVideoFormat_DVSL); //     FCC('dvsl')
	IF_EQUAL_RETURN(guid, MFVideoFormat_H264); //     FCC('H264')
	IF_EQUAL_RETURN(guid, MFVideoFormat_I420); //     FCC('I420')
	IF_EQUAL_RETURN(guid, MFVideoFormat_IYUV); //     FCC('IYUV')
	IF_EQUAL_RETURN(guid, MFVideoFormat_M4S2); //     FCC('M4S2')
	IF_EQUAL_RETURN(guid, MFVideoFormat_MJPG);
	IF_EQUAL_RETURN(guid, MFVideoFormat_MP43); //     FCC('MP43')
	IF_EQUAL_RETURN(guid, MFVideoFormat_MP4S); //     FCC('MP4S')
	IF_EQUAL_RETURN(guid, MFVideoFormat_MP4V); //     FCC('MP4V')
	IF_EQUAL_RETURN(guid, MFVideoFormat_MPG1); //     FCC('MPG1')
	IF_EQUAL_RETURN(guid, MFVideoFormat_MSS1); //     FCC('MSS1')
	IF_EQUAL_RETURN(guid, MFVideoFormat_MSS2); //     FCC('MSS2')
	IF_EQUAL_RETURN(guid, MFVideoFormat_NV11); //     FCC('NV11')
	IF_EQUAL_RETURN(guid, MFVideoFormat_NV12); //     FCC('NV12')
	IF_EQUAL_RETURN(guid, MFVideoFormat_P010); //     FCC('P010')
	IF_EQUAL_RETURN(guid, MFVideoFormat_P016); //     FCC('P016')
	IF_EQUAL_RETURN(guid, MFVideoFormat_P210); //     FCC('P210')
	IF_EQUAL_RETURN(guid, MFVideoFormat_P216); //     FCC('P216')
	IF_EQUAL_RETURN(guid, MFVideoFormat_RGB24); //    D3DFMT_R8G8B8 
	IF_EQUAL_RETURN(guid, MFVideoFormat_RGB32); //    D3DFMT_X8R8G8B8 
	IF_EQUAL_RETURN(guid, MFVideoFormat_RGB555); //   D3DFMT_X1R5G5B5 
	IF_EQUAL_RETURN(guid, MFVideoFormat_RGB565); //   D3DFMT_R5G6B5 
	IF_EQUAL_RETURN(guid, MFVideoFormat_RGB8);
	IF_EQUAL_RETURN(guid, MFVideoFormat_UYVY); //     FCC('UYVY')
	IF_EQUAL_RETURN(guid, MFVideoFormat_v210); //     FCC('v210')
	IF_EQUAL_RETURN(guid, MFVideoFormat_v410); //     FCC('v410')
	IF_EQUAL_RETURN(guid, MFVideoFormat_WMV1); //     FCC('WMV1')
	IF_EQUAL_RETURN(guid, MFVideoFormat_WMV2); //     FCC('WMV2')
	IF_EQUAL_RETURN(guid, MFVideoFormat_WMV3); //     FCC('WMV3')
	IF_EQUAL_RETURN(guid, MFVideoFormat_WVC1); //     FCC('WVC1')
	IF_EQUAL_RETURN(guid, MFVideoFormat_Y210); //     FCC('Y210')
	IF_EQUAL_RETURN(guid, MFVideoFormat_Y216); //     FCC('Y216')
	IF_EQUAL_RETURN(guid, MFVideoFormat_Y410); //     FCC('Y410')
	IF_EQUAL_RETURN(guid, MFVideoFormat_Y416); //     FCC('Y416')
	IF_EQUAL_RETURN(guid, MFVideoFormat_Y41P);
	IF_EQUAL_RETURN(guid, MFVideoFormat_Y41T);
	IF_EQUAL_RETURN(guid, MFVideoFormat_YUY2); //     FCC('YUY2')
	IF_EQUAL_RETURN(guid, MFVideoFormat_YV12); //     FCC('YV12')
	IF_EQUAL_RETURN(guid, MFVideoFormat_YVYU);

	IF_EQUAL_RETURN(guid, MFAudioFormat_PCM); //              WAVE_FORMAT_PCM 
	IF_EQUAL_RETURN(guid, MFAudioFormat_Float); //            WAVE_FORMAT_IEEE_FLOAT 
	IF_EQUAL_RETURN(guid, MFAudioFormat_DTS); //              WAVE_FORMAT_DTS 
	IF_EQUAL_RETURN(guid, MFAudioFormat_Dolby_AC3_SPDIF); //  WAVE_FORMAT_DOLBY_AC3_SPDIF 
	IF_EQUAL_RETURN(guid, MFAudioFormat_DRM); //              WAVE_FORMAT_DRM 
	IF_EQUAL_RETURN(guid, MFAudioFormat_WMAudioV8); //        WAVE_FORMAT_WMAUDIO2 
	IF_EQUAL_RETURN(guid, MFAudioFormat_WMAudioV9); //        WAVE_FORMAT_WMAUDIO3 
	IF_EQUAL_RETURN(guid, MFAudioFormat_WMAudio_Lossless); // WAVE_FORMAT_WMAUDIO_LOSSLESS 
	IF_EQUAL_RETURN(guid, MFAudioFormat_WMASPDIF); //         WAVE_FORMAT_WMASPDIF 
	IF_EQUAL_RETURN(guid, MFAudioFormat_MSP1); //             WAVE_FORMAT_WMAVOICE9 
	IF_EQUAL_RETURN(guid, MFAudioFormat_MP3); //              WAVE_FORMAT_MPEGLAYER3 
	IF_EQUAL_RETURN(guid, MFAudioFormat_MPEG); //             WAVE_FORMAT_MPEG 
	IF_EQUAL_RETURN(guid, MFAudioFormat_AAC); //              WAVE_FORMAT_MPEG_HEAAC 
	IF_EQUAL_RETURN(guid, MFAudioFormat_ADTS); //             WAVE_FORMAT_MPEG_ADTS_AAC 

	return NULL;
}

HRESULT GetDefaultStride(IMFMediaType* pType, LONG* plStride)
{
	LONG lStride = 0;

	// Try to get the default stride from the media type.
	HRESULT hr = pType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&lStride);
	if (FAILED(hr))
	{
		// Attribute not set. Try to calculate the default stride.

		GUID subtype = GUID_NULL;

		UINT32 width = 0;
		UINT32 height = 0;

		// Get the subtype and the image size.
		hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
		if (FAILED(hr))
		{
			goto done;
		}

		hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
		if (FAILED(hr))
		{
			goto done;
		}

		hr = MFGetStrideForBitmapInfoHeader(subtype.Data1, width, &lStride);
		if (FAILED(hr))
		{
			goto done;
		}

		// Set the attribute for later reference.
		(void)pType->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(lStride));
	}

	if (SUCCEEDED(hr))
	{
		*plStride = lStride;
	}

done:
	return hr;
}

QString HrToString(HRESULT hr)
{
	wchar_t* buf = nullptr;

	DWORD len = FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		static_cast<DWORD>(hr),
		MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
		(LPWSTR)&buf,
		0,
		nullptr);

	QString msg;
	if (len && buf)
	{
		msg = QString::fromWCharArray(buf).trimmed();
		LocalFree(buf);
	}
	else
	{
		msg = "Unknown error";
	}

	return QString("0x%1 (%2)")
		.arg(static_cast<quint32>(hr), 8, 16, QChar('0'))
		.arg(msg);
}

static bool FindVideoCaptureFilterByName(const QString& friendlyName, IBaseFilter** ppFilter)
{
	if (!ppFilter) return false;
	*ppFilter = nullptr;

	ICreateDevEnum* pDevEnum = nullptr;
	IEnumMoniker* pEnum = nullptr;

	HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&pDevEnum));
	if (FAILED(hr)) return false;

	hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnum, 0);
	if (hr != S_OK)
	{
		pDevEnum->Release();
		return false;
	}

	IMoniker* pMoniker = nullptr;
	ULONG fetched = 0;
	bool found = false;

	while (pEnum->Next(1, &pMoniker, &fetched) == S_OK)
	{
		IPropertyBag* pBag = nullptr;
		hr = pMoniker->BindToStorage(0, 0, IID_PPV_ARGS(&pBag));
		if (SUCCEEDED(hr))
		{
			VARIANT varName;
			VariantInit(&varName);

			hr = pBag->Read(L"FriendlyName", &varName, 0);
			if (SUCCEEDED(hr) && varName.vt == VT_BSTR)
			{
				QString name = QString::fromWCharArray(varName.bstrVal);
				VARIANT varDevicePath;
				VariantInit(&varDevicePath);
				hr = pBag->Read(L"DevicePath", &varDevicePath, 0);
				QString strUniqueId;
				if (SUCCEEDED(hr) && varDevicePath.vt == VT_BSTR)
				{
					QString devicePath = QString::fromWCharArray(varDevicePath.bstrVal);
					qDebug() << "Found video capture device: " << name << ", path: " << devicePath;
					std::string id;
					EZCamera::extractUniqueId(devicePath.toStdString(), id);
					strUniqueId = QString::fromStdString(id);
				}

				QString strNameWithUniqueId = name + "_" + strUniqueId;
				if (strNameWithUniqueId == friendlyName)
				{
					hr = pMoniker->BindToObject(nullptr, nullptr, IID_PPV_ARGS(ppFilter));
					if (SUCCEEDED(hr))
					{
						found = true;
					}
				}

				VariantClear(&varDevicePath);
			}

			VariantClear(&varName);
			pBag->Release();
		}

		pMoniker->Release();

		if (found)
			break;
	}

	pEnum->Release();
	pDevEnum->Release();

	return found;
}

HRESULT GetAllocatedString(
	IMFActivate* pActivate,
	REFGUID key,
	QString& outStr)
{
	WCHAR* buf = nullptr;
	UINT32 cch = 0;
	HRESULT hr = pActivate->GetAllocatedString(key, &buf, &cch);
	if (SUCCEEDED(hr) && buf)
	{
		outStr = QString::fromWCharArray(buf);
		CoTaskMemFree(buf);
	}
	return hr;
}

#endif

EZCamera::EZCamera(QObject *parent, QString strDeviceName)
	: QObject(parent)
{
	this->m_strName = strDeviceName;
	qDebug() << "New Camera created.";
}

EZCamera::~EZCamera()
{
	qDebug() << "~EZCamera on thread" << QThread::currentThread();
}

void EZCamera::start()
{
#ifdef Q_OS_WIN
	this->CreateVideoDeviceSource(&this->m_pMediaSource);
	if (nullptr == this->m_pMediaSource)
	{
		QString strError = QString("Failed to create media source for device: %1").arg(this->m_strName);
		QCoreApplication::postEvent(this->m_pRenderWidget, new EZCameraDeviceErrorEvent(strError, 123));
		return;
	}

	this->SetHighestNV12(this->m_pMediaSource);

	IMFSourceReader* pReader = nullptr;

	HRESULT hr = MFCreateSourceReaderFromMediaSource(
		static_cast<IMFMediaSource*>(this->m_pMediaSource),
		nullptr,        // 关键：同步模式，不设置 MF_SOURCE_READER_ASYNC_CALLBACK
		&pReader);

	if (FAILED(hr) || nullptr == pReader)
	{
		QString strError = QString("MFCreateSourceReaderFromMediaSource failed: %1").arg(HrToString(hr));
		QCoreApplication::postEvent(this->m_pRenderWidget, new EZCameraDeviceErrorEvent(strError, 123));

		IMFMediaSource* pSource = static_cast<IMFMediaSource*>(this->m_pMediaSource);
		if (pSource)
		{
			pSource->Shutdown();
			pSource->Release();
			this->m_pMediaSource = nullptr;
		}

		return;
	}

	this->m_pSourceReader = pReader;
	this->m_bIsRunning.store(true, std::memory_order_release);

	// 先发空帧，避免界面显示旧图
	this->handleFrame(nullptr, 0, 0, 0);

	emit signalFrameInfo(this->getFrameInfo());

	qDebug() << this->m_strName << " started in sync mode.";

	// 不要 while 循环，丢回 Qt 事件队列读第一帧
	QMetaObject::invokeMethod(this, "readOneFrame", Qt::QueuedConnection);
#endif
}

void EZCamera::stop()
{
#ifdef Q_OS_WIN
	if (!this->m_bIsRunning.load(std::memory_order_acquire))
	{
		qDebug() << "m_bIsRunning == false";
	}

	this->m_bIsRunning.store(false, std::memory_order_release);

	IMFSourceReader* pReader = static_cast<IMFSourceReader*>(this->m_pSourceReader);
	IMFMediaSource* pSource = static_cast<IMFMediaSource*>(this->m_pMediaSource);
	IMFAttributes* pAttr = static_cast<IMFAttributes*>(this->m_pAttributes);

	if (pReader)
	{
		pReader->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
		pReader->Release();
		this->m_pSourceReader = nullptr;
	}

	if (pAttr)
	{
		pAttr->Release();
		this->m_pAttributes = nullptr;
	}

	if (pSource)
	{
		pSource->Shutdown();
		pSource->Release();
		this->m_pMediaSource = nullptr;
	}

	qDebug() << this->m_strName << " stopped.";
#endif
}

void EZCamera::onFrameConsumed()
{
	m_framePending.store(false, std::memory_order_release);
}

void EZCamera::readOneFrame()
{
#ifdef Q_OS_WIN
	if (!this->m_bIsRunning.load(std::memory_order_acquire))
	{
		return;
	}

	IMFSourceReader* pReader = static_cast<IMFSourceReader*>(this->m_pSourceReader);
	if (nullptr == pReader)
	{
		return;
	}

	DWORD streamIndex = 0;
	DWORD flags = 0;
	LONGLONG timestamp = 0;
	IMFSample* pSample = nullptr;

	HRESULT hr = pReader->ReadSample(
		MF_SOURCE_READER_FIRST_VIDEO_STREAM,
		0,
		&streamIndex,
		&flags,
		&timestamp,
		&pSample);

	if (FAILED(hr))
	{
		this->m_bIsRunning.store(false, std::memory_order_release);

		QString strError = QString("ReadSample failed: %1").arg(HrToString(hr));
		qDebug() << strError;

		QCoreApplication::postEvent(
			this->m_pRenderWidget,
			new EZCameraDeviceErrorEvent(strError, 123));

		if (pSample)
		{
			pSample->Release();
			pSample = nullptr;
		}

		return;
	}

	if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
	{
		this->m_bIsRunning.store(false, std::memory_order_release);

		if (pSample)
		{
			pSample->Release();
			pSample = nullptr;
		}

		return;
	}

	if (pSample)
	{
		IMFMediaType* pType = nullptr;
		UINT32 width = 0;
		UINT32 height = 0;
		LONG stride = 0;

		hr = pReader->GetCurrentMediaType(
			MF_SOURCE_READER_FIRST_VIDEO_STREAM,
			&pType);

		if (SUCCEEDED(hr) && pType)
		{
			MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);

			LONG lStride = 0;
			if (SUCCEEDED(GetDefaultStride(pType, &lStride)))
			{
				stride = lStride;
			}

			pType->Release();
			pType = nullptr;
		}

		IMFMediaBuffer* pBuffer = nullptr;
		hr = pSample->ConvertToContiguousBuffer(&pBuffer);

		if (SUCCEEDED(hr) && pBuffer)
		{
			BYTE* pData = nullptr;
			DWORD maxLen = 0;
			DWORD curLen = 0;

			hr = pBuffer->Lock(&pData, &maxLen, &curLen);
			if (SUCCEEDED(hr) && pData && width > 0 && height > 0 && stride > 0)
			{
				this->handleFrame(pData, width, height, stride);
				pBuffer->Unlock();
			}

			pBuffer->Release();
			pBuffer = nullptr;
		}

		pSample->Release();
		pSample = nullptr;
	}

	// 继续读下一帧，但不要 while 无限循环
	if (this->m_bIsRunning.load(std::memory_order_acquire))
	{
		QMetaObject::invokeMethod(this, "readOneFrame", Qt::QueuedConnection);
	}
#endif
}

void EZCamera::handleFrame(void* data, int width, int height, int stride)
{
	if (!this->m_bIsRunning.load(std::memory_order_acquire))
	{
		return;
	}

	// GUI 还有一帧没处理完，就直接丢掉当前帧
	bool expected = false;
	if (!m_framePending.compare_exchange_strong(expected, true))
	{
		return;
	}

	const int yBytes = stride * height;
	const int uvBytes = stride * height / 2;
	const int total = yBytes + uvBytes;

	QByteArray buf;
	if (data != nullptr && total > 0)
	{
		buf.resize(total);
		memcpy(buf.data(), data, size_t(total));
	}

	emit signalFrameReady(buf, width, height, stride);
}

QString EZCamera::getFrameInfo() const
{
	QString strInfo = QString("%1x%2 %3 %4fps")
						.arg(m_nFrameWidth)
						.arg(m_nFrameHeight)
						.arg(m_strFormat)
						.arg(m_nFPS);

	return strInfo;
}

bool EZCamera::getExposureRange(long& min, long& max, long& step, long& def, long& flags) const
{
	if (nullptr == this->m_pCamCtrl)
	{
		return false;
	}

	IAMCameraControl* pCamCtrl = (IAMCameraControl*)this->m_pCamCtrl;
	HRESULT hr = pCamCtrl->GetRange(CameraControl_Exposure, &min, &max, &step, &def, &flags);

	return SUCCEEDED(hr);
}

bool EZCamera::getExposureRangeTest(QString strCameraName, long& min, long& max, long& step, long& def, long& flags)
{
	IBaseFilter* pFilter = nullptr;
	if (!FindVideoCaptureFilterByName(strCameraName, &pFilter))
		return false;

	IAMCameraControl* pCamCtrl = nullptr;
	HRESULT hr = pFilter->QueryInterface(IID_PPV_ARGS(&pCamCtrl));
	pFilter->Release();
	if (FAILED(hr) || !pCamCtrl)
		return false;

	hr = pCamCtrl->GetRange(CameraControl_Exposure, &min, &max, &step, &def, &flags);
	pCamCtrl->Release();

	return SUCCEEDED(hr);
}

bool EZCamera::isAutoExposureSupported(long& flags) const
{
	bool bSupportsAuto = (flags & CameraControl_Flags_Auto) != 0;

	return bSupportsAuto;
}

bool EZCamera::setExposureAuto(bool enable)
{
	if (nullptr == this->m_pCamCtrl)
	{
		return false;
	}

	IAMCameraControl* pCamCtrl = (IAMCameraControl*)this->m_pCamCtrl;
	long flags = enable ? CameraControl_Flags_Auto : CameraControl_Flags_Manual;

	// 自动模式下 value 多数驱动不看，给 0 即可
	HRESULT hr = pCamCtrl->Set(CameraControl_Exposure, 0, flags);

	return SUCCEEDED(hr);
}

bool EZCamera::setExposureValue(long value)
{
	if (nullptr == this->m_pCamCtrl)
	{
		return false;
	}

	IAMCameraControl* pCamCtrl = (IAMCameraControl*)this->m_pCamCtrl;
	HRESULT hr = pCamCtrl->Set(CameraControl_Exposure, value, CameraControl_Flags_Manual);

	return SUCCEEDED(hr);
}

bool EZCamera::getExposureValue(long& value, long& flags) const
{
	if (nullptr == this->m_pCamCtrl)
	{
		return false;
	}
	IAMCameraControl* pCamCtrl = (IAMCameraControl*)this->m_pCamCtrl;
	HRESULT hr = pCamCtrl->Get(CameraControl_Exposure, &value, &flags);

	return SUCCEEDED(hr);
}

bool EZCamera::getVideoProcAmpRange(long property, long& min, long& max, long& step, long& def, long& flags) const
{
	if (nullptr == this->m_pVideoProcAmp)
	{
		return false;
	}

	IAMVideoProcAmp* pProc = (IAMVideoProcAmp*)this->m_pVideoProcAmp;
	HRESULT hr = pProc->GetRange(property, &min, &max, &step, &def, &flags);

	return SUCCEEDED(hr);
}

bool EZCamera::setVideoProcAmpRange(long property, long value)
{
	if (nullptr == this->m_pVideoProcAmp)
	{
		return false;
	}

	IAMVideoProcAmp* pProc = (IAMVideoProcAmp*)this->m_pVideoProcAmp;
	HRESULT hr = pProc->Set(property, value, VideoProcAmp_Flags_Manual);

	return SUCCEEDED(hr);
}

bool EZCamera::setVideoProcAmpAuto(long property, bool enable)
{
	if (nullptr == this->m_pVideoProcAmp)
	{
		return false;
	}

	IAMVideoProcAmp* pProc = (IAMVideoProcAmp*)this->m_pVideoProcAmp;
	long flags = enable ? VideoProcAmp_Flags_Auto : VideoProcAmp_Flags_Manual;
	HRESULT hr = pProc->Set(property, 0, flags);

	return SUCCEEDED(hr);
}

bool EZCamera::isAutoBrightnessSupported(long& flags) const
{
	bool bSupportsAuto = (flags & VideoProcAmp_Flags_Auto) != 0;

	return bSupportsAuto;
}

bool EZCamera::getBrightnessRange(long& min, long& max, long& step, long& def, long& flags) const
{
	return this->getVideoProcAmpRange(VideoProcAmp_Brightness, min, max, step, def, flags);
}

bool EZCamera::setBrightnessValue(long value)
{
	return this->setVideoProcAmpRange(VideoProcAmp_Brightness, value);
}

bool EZCamera::getBrightnessValue(long& value, long& flags) const
{
	if (nullptr == this->m_pVideoProcAmp)
	{
		return false;
	}

	IAMVideoProcAmp* pProc = (IAMVideoProcAmp*)this->m_pVideoProcAmp;
	HRESULT hr = pProc->Get(VideoProcAmp_Brightness, &value, &flags);

	return SUCCEEDED(hr);
}

bool EZCamera::setBrightnessAuto(bool enable)
{
	return this->setVideoProcAmpAuto(VideoProcAmp_Brightness, enable);
}

bool EZCamera::isAutoContrastSupported(long& flags) const
{
	bool bSupportsAuto = (flags & VideoProcAmp_Flags_Auto) != 0;

	return bSupportsAuto;
}

bool EZCamera::getContrastRange(long& min, long& max, long& step, long& def, long& flags) const
{
	return this->getVideoProcAmpRange(VideoProcAmp_Contrast, min, max, step, def, flags);
}

bool EZCamera::setContrastValue(long value)
{
	return this->setVideoProcAmpRange(VideoProcAmp_Contrast, value);
}

bool EZCamera::getContrastValue(long& value, long& flags) const
{
	if (nullptr == this->m_pVideoProcAmp)
	{
		return false;
	}
	IAMVideoProcAmp* pProc = (IAMVideoProcAmp*)this->m_pVideoProcAmp;
	HRESULT hr = pProc->Get(VideoProcAmp_Contrast, &value, &flags);

	return SUCCEEDED(hr);
}

bool EZCamera::setContrastAuto(bool enable)
{
	return this->setVideoProcAmpAuto(VideoProcAmp_Contrast, enable);
}

QList<CameraInfo> EZCamera::getAvailableCameraNames()
{
	QList<CameraInfo> lst;
	IMFAttributes* pAttributes = NULL;
	IMFActivate** ppDevices = NULL;

	// Create an attribute store to specify enumeration parameters.
	HRESULT hr = MFCreateAttributes(&pAttributes, 1);
	if (FAILED(hr))
	{
		goto done;
	}

	// Source type: video capture devices.
	hr = pAttributes->SetGUID(
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
	);
	if (FAILED(hr))
	{
		goto done;
	}

	// Enumerate devices.
	UINT32 count;
	hr = MFEnumDeviceSources(pAttributes, &ppDevices, &count);
	if (FAILED(hr))
	{
		goto done;
	}
	if (count == 0)
	{
		goto done;
	}
	for (int i = 0; i < count; i++)
	{
		WCHAR* szFriendlyName = NULL;
		UINT32 cchName;
		ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &szFriendlyName, &cchName);
		QString strName = QString::fromWCharArray(szFriendlyName);
		CoTaskMemFree(szFriendlyName);

		WCHAR* szSymbolicLink = NULL;
		UINT32 cchLink;
		ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &szSymbolicLink, &cchLink);
		QString strSymbolicLink = QString::fromWCharArray(szSymbolicLink);
		std::string strUniqueId;
		EZCamera::extractUniqueId(strSymbolicLink.toStdString(), strUniqueId);
		CoTaskMemFree(szSymbolicLink);

		strName = strName + "_" + QString::fromStdString(strUniqueId);
		lst.append(CameraInfo(strName, strSymbolicLink));
	}

done:
	SafeRelease(&pAttributes);
	for (DWORD i = 0; i < count; i++)
	{
		SafeRelease(&ppDevices[i]);
	}
	CoTaskMemFree(ppDevices);

	return lst;
}

bool EZCamera::CreateVideoDeviceSource(void** ppSource)
{
	*ppSource = NULL;

	IMFMediaSource* pSource = NULL;
	IMFAttributes* pAttributes = NULL;
	IMFActivate** ppDevices = NULL;

	// Create an attribute store to specify enumeration parameters.
	HRESULT hr = MFCreateAttributes(&pAttributes, 1);
	if (FAILED(hr))
	{
		goto done;
	}

	// Source type: video capture devices.
	hr = pAttributes->SetGUID(
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
	);
	if (FAILED(hr))
	{
		goto done;
	}

	// Enumerate devices.
	UINT32 count;
	hr = MFEnumDeviceSources(pAttributes, &ppDevices, &count);
	if (FAILED(hr))
	{
		goto done;
	}
	if (count == 0)
	{
		hr = E_FAIL;
		goto done;
	}

	// Create the media source object.
	for (int i = 0; i < count; i++)
	{
		IMFActivate* pActivate = ppDevices[i];
		WCHAR* szFriendlyName = NULL;
		UINT32 cchName;
		pActivate->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &szFriendlyName, &cchName);
		QString strName = QString::fromWCharArray(szFriendlyName);
		CoTaskMemFree(szFriendlyName);

		WCHAR* szSymbolicLink = NULL;
		UINT32 cchLink;
		ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &szSymbolicLink, &cchLink);
		QString strSymbolicLink = QString::fromWCharArray(szSymbolicLink);
		std::string strUniqueId;
		EZCamera::extractUniqueId(strSymbolicLink.toStdString(), strUniqueId);
		CoTaskMemFree(szSymbolicLink);
		strName = strName + "_" + QString::fromStdString(strUniqueId);
		if (this->m_strName == strName)
		{
			hr = pActivate->ActivateObject(IID_PPV_ARGS(&pSource));
			if (FAILED(hr))
			{
				goto done;
			}

			break;
		}
	}

	if (pSource != nullptr)
	{
		*ppSource = pSource;
		((IMFMediaSource*)(*ppSource))->AddRef();
	}

done:
	SafeRelease(&pAttributes);

	for (DWORD i = 0; i < count; i++)
	{
		SafeRelease(&ppDevices[i]);
	}
	CoTaskMemFree(ppDevices);
	SafeRelease(&pSource);

	bool bRet = pSource != nullptr;

	return bRet;
}

void EZCamera::EnumerateCaptureFormats(void* v_pSource)
{
	IMFMediaSource* pSource = (IMFMediaSource*)v_pSource;
	IMFPresentationDescriptor* pPD = NULL;
	IMFStreamDescriptor* pSD = NULL;
	IMFMediaTypeHandler* pHandler = NULL;
	IMFMediaType* pType = NULL;
	DWORD cTypes = 0;

	HRESULT hr = pSource->CreatePresentationDescriptor(&pPD);
	if (FAILED(hr))
	{
		goto done;
	}

	BOOL fSelected;
	hr = pPD->GetStreamDescriptorByIndex(0, &fSelected, &pSD);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = pSD->GetMediaTypeHandler(&pHandler);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = pHandler->GetMediaTypeCount(&cTypes);
	if (FAILED(hr))
	{
		goto done;
	}

	for (DWORD i = 0; i < cTypes; i++)
	{
		hr = pHandler->GetMediaTypeByIndex(i, &pType);
		if (FAILED(hr))
		{
			goto done;
		}

		LogMediaType(pType);
		OutputDebugString(L"\n");

		SafeRelease(&pType);
	}

done:
	SafeRelease(&pPD);
	SafeRelease(&pSD);
	SafeRelease(&pHandler);
	SafeRelease(&pType);
}

void EZCamera::SetDeviceFormat(void* v_pSource, unsigned long dwFormatIndex)
{
	IMFMediaSource* pSource = (IMFMediaSource*)v_pSource;

	IMFPresentationDescriptor* pPD = NULL;
	IMFStreamDescriptor* pSD = NULL;
	IMFMediaTypeHandler* pHandler = NULL;
	IMFMediaType* pType = NULL;

	HRESULT hr = pSource->CreatePresentationDescriptor(&pPD);
	if (FAILED(hr))
	{
		goto done;
	}

	BOOL fSelected;
	hr = pPD->GetStreamDescriptorByIndex(0, &fSelected, &pSD);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = pSD->GetMediaTypeHandler(&pHandler);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = pHandler->GetMediaTypeByIndex(dwFormatIndex, &pType);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = pHandler->SetCurrentMediaType(pType);

done:
	SafeRelease(&pPD);
	SafeRelease(&pSD);
	SafeRelease(&pHandler);
	SafeRelease(&pType);
}

void EZCamera::SetDeviceMaxFrameRate(void* v_pSource, unsigned long dwTypeIndex)
{
	IMFMediaSource* pSource = (IMFMediaSource*)v_pSource;
	IMFPresentationDescriptor* pPD = NULL;
	IMFStreamDescriptor* pSD = NULL;
	IMFMediaTypeHandler* pHandler = NULL;
	IMFMediaType* pType = NULL;

	HRESULT hr = pSource->CreatePresentationDescriptor(&pPD);
	if (FAILED(hr))
	{
		goto done;
	}

	BOOL fSelected;
	hr = pPD->GetStreamDescriptorByIndex(dwTypeIndex, &fSelected, &pSD);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = pSD->GetMediaTypeHandler(&pHandler);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = pHandler->GetCurrentMediaType(&pType);
	if (FAILED(hr))
	{
		goto done;
	}

	// Get the maximum frame rate for the selected capture format.

	// Note: To get the minimum frame rate, use the 
	// MF_MT_FRAME_RATE_RANGE_MIN attribute instead.

	PROPVARIANT var;
	if (SUCCEEDED(pType->GetItem(MF_MT_FRAME_RATE_RANGE_MAX, &var)))
	{
		hr = pType->SetItem(MF_MT_FRAME_RATE, var);

		PropVariantClear(&var);

		if (FAILED(hr))
		{
			goto done;
		}

		hr = pHandler->SetCurrentMediaType(pType);
	}

done:
	SafeRelease(&pPD);
	SafeRelease(&pSD);
	SafeRelease(&pHandler);
	SafeRelease(&pType);
}

long EZCamera::SetHighestNV12(void* v_pSource)
{
	IMFMediaSource* pSource = (IMFMediaSource*)v_pSource;
	if (!pSource) return E_POINTER;

	IMFPresentationDescriptor* pPD = nullptr;
	IMFStreamDescriptor* pSD = nullptr;
	IMFMediaTypeHandler* pHandler = nullptr;
	IMFMediaType* pType = nullptr;

	HRESULT hr = S_OK;
	BOOL fSelected = FALSE;

	UINT32 bestWidth = 0, bestHeight = 0;
	IMFMediaType* pBestType = nullptr;
	UINT32 fpsNum = 0, fpsDen = 0;

	// 1. 取 PresentationDescriptor
	hr = pSource->CreatePresentationDescriptor(&pPD);
	if (FAILED(hr)) goto done;


	// 2. 只取第 0 个视频流（大多数摄像头只有 1 个）
	hr = pPD->GetStreamDescriptorByIndex(0, &fSelected, &pSD);
	if (FAILED(hr)) goto done;

	// 3. 获取 MediaTypeHandler
	hr = pSD->GetMediaTypeHandler(&pHandler);
	if (FAILED(hr)) goto done;

	// 4. 枚举 MediaType
	for (DWORD index = 0; ; index++)
	{
		SafeRelease(&pType);

		hr = pHandler->GetMediaTypeByIndex(index, &pType);
		if (FAILED(hr))
		{
			// 全部枚举完了
			hr = MF_E_NO_MORE_TYPES;
			break;
		}

		// 检查 subtype 是否是 NV12
		GUID subtype = { 0 };
		hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
		if (FAILED(hr)) continue;

		if (subtype != MFVideoFormat_NV12)
			continue;

		// 读取分辨率
		UINT32 w = 0, h = 0;
		hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &w, &h);
		if (FAILED(hr)) continue;

		UINT64 pixels = (UINT64)w * h;

		UINT64 bestPixels = (UINT64)bestWidth * bestHeight;

		// 找最大分辨率
		if (pixels > bestPixels)
		{
			bestWidth = w;
			bestHeight = h;

			SafeRelease(&pBestType);
			pBestType = pType;
			pType = nullptr; // 让 pBestType 持有这个指针
		}
	}

	if (bestWidth == 0)
	{
		qDebug() << "No NV12 type found!";
		hr = E_FAIL;
		goto done;
	}

	qDebug() << "Best NV12 =" << bestWidth << "x" << bestHeight;
	LogMediaType(pBestType);

	this->m_nFrameWidth = bestWidth;
	this->m_nFrameHeight = bestHeight;
	this->m_strFormat = "NV12";
	// 读取帧率
	hr = MFGetAttributeRatio(pBestType, MF_MT_FRAME_RATE, &fpsNum, &fpsDen);
	if (SUCCEEDED(hr) && fpsDen != 0)
	{
		double fps = (double)fpsNum / fpsDen;
		this->m_nFPS = fps;
		qDebug() << "FrameRate =" << fpsNum << "/" << fpsDen << "=" << fps << "FPS";
	}
	else
	{
		qDebug() << "FrameRate not found";
	}

	// 5. 设置为当前格式
	hr = pHandler->SetCurrentMediaType(pBestType);
	if (FAILED(hr))
	{
		qDebug() << "SetCurrentMediaType failed";
		goto done;
	}

done:
	SafeRelease(&pPD);
	SafeRelease(&pSD);
	SafeRelease(&pHandler);
	SafeRelease(&pType);
	SafeRelease(&pBestType);

	return hr;
}

bool EZCamera::initControlInterfaces()
{
	this->releaseControlInterfaces();

	IBaseFilter* pFilter = nullptr;
	if (!FindVideoCaptureFilterByName(this->m_strName, &pFilter))
	{
		return false;
	}
	if (pFilter != nullptr)
	{
		this->m_pCapFilter = pFilter;

		IAMCameraControl* pCamCtrl = nullptr;
		if (SUCCEEDED(pFilter->QueryInterface(IID_PPV_ARGS(&pCamCtrl))))
		{
			this->m_pCamCtrl = pCamCtrl;
		}
		IAMVideoProcAmp* pProcAmp = nullptr;
		if (SUCCEEDED(pFilter->QueryInterface(IID_PPV_ARGS(&pProcAmp))))
		{
			this->m_pVideoProcAmp = pProcAmp;
		}
	}

	return (this->m_pCapFilter != nullptr && this->m_pCamCtrl != nullptr && this->m_pVideoProcAmp != nullptr);
}

void EZCamera::releaseControlInterfaces()
{
	if (this->m_pVideoProcAmp)
	{
		((IAMVideoProcAmp*)this->m_pVideoProcAmp)->Release();
		this->m_pVideoProcAmp = nullptr;
	}
	if (this->m_pCamCtrl)
	{
		((IAMCameraControl*)this->m_pCamCtrl)->Release();
		this->m_pCamCtrl = nullptr;
	}
	if (this->m_pCapFilter)
	{
		((IBaseFilter*)this->m_pCapFilter)->Release();
		this->m_pCapFilter = nullptr;
	}
}

bool EZCamera::extractVidPid(const std::string& strSymbolicLink, std::string& vid, std::string& pid)
{
	// 示例格式：\\?\usb#vid_046d&pid_0825&mi_00#7&2b9cbb1b&0&0000#{e5323777-f976-4f5b-9b55-b94699c46e44}\global
	// 不区分大小写匹配 vid_xxxx 和 pid_xxxx
	std::regex re(R"((vid_[0-9a-fA-F]{4}).*?(pid_[0-9a-fA-F]{4}))", std::regex::icase);
	std::smatch match;

	if (std::regex_search(strSymbolicLink, match, re))
	{
		vid = match[1].str();
		pid = match[2].str();

		// 转成小写，便于统一
		std::transform(vid.begin(), vid.end(), vid.begin(), ::tolower);
		std::transform(pid.begin(), pid.end(), pid.begin(), ::tolower);
		if (vid.find("vid_") == 0)
		{
			vid = vid.substr(4);
		}
		if (pid.find("pid_") == 0)
		{
			pid = pid.substr(4);
		}

		return true;
	}

	return false;
}

bool EZCamera::extractUniqueId(const std::string& strSymbolicLink, std::string& uniqueId)
{
	std::string path = strSymbolicLink;
	uniqueId.clear();

	size_t p1 = path.find('#');
	if (p1 == std::string::npos) return false;

	size_t p2 = path.find('#', p1 + 1);
	if (p2 == std::string::npos) return false;

	size_t p3 = path.find('#', p2 + 1);
	if (p3 == std::string::npos) return false;

	std::string instancePart = path.substr(p2 + 1, p3 - p2 - 1);
	// 例如：7&2b9cbb1b&0&0000

	size_t a1 = instancePart.find('&');
	if (a1 == std::string::npos) return false;

	size_t a2 = instancePart.find('&', a1 + 1);
	if (a2 == std::string::npos) return false;

	uniqueId = instancePart.substr(a1 + 1, a2 - a1 - 1);
	return !uniqueId.empty();
}

