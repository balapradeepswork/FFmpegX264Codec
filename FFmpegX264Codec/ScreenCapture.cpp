
#include "ScreenCapture.h"
#include "time.h"

D3D_DRIVER_TYPE gDriverTypes[] =
{
	D3D_DRIVER_TYPE_HARDWARE
};
UINT gNumDriverTypes = ARRAYSIZE(gDriverTypes);

// Feature levels supported
D3D_FEATURE_LEVEL gFeatureLevels[] =
{
	D3D_FEATURE_LEVEL_11_0,
	D3D_FEATURE_LEVEL_10_1,
	D3D_FEATURE_LEVEL_10_0,
	D3D_FEATURE_LEVEL_9_1
};

UINT gNumFeatureLevels = ARRAYSIZE(gFeatureLevels);

ScreenCaptureProcessorGDI::ScreenCaptureProcessorGDI() 
	{
		
	}


RPCScreenCapture::RPCScreenCapture(void)
{
}

RPCScreenCapture::~RPCScreenCapture(void)
{
}

bool ScreenCaptureProcessorGDI::init()
	{
		// Create device			
		for (UINT DriverTypeIndex = 0; DriverTypeIndex < gNumDriverTypes; ++DriverTypeIndex)
		{
			hr = D3D11CreateDevice(
				nullptr,
				gDriverTypes[DriverTypeIndex],
				nullptr,
				0,
				gFeatureLevels,
				gNumFeatureLevels,
				D3D11_SDK_VERSION,
				&lDevice,
				&lFeatureLevel,
				&lImmediateContext);

			if (SUCCEEDED(hr))
			{
				// Device creation success, no need to loop anymore
				break;
			}

			lDevice.Release();

			lImmediateContext.Release();
		}

		if (FAILED(hr))
		{
			return false;
		}

		if (lDevice == nullptr)
		{
			return false;
		}

		// Get DXGI device
		CComPtrCustom<IDXGIDevice> lDxgiDevice;

		hr = lDevice->QueryInterface(IID_PPV_ARGS(&lDxgiDevice));

		if (FAILED(hr))
		{
			return false;
		}

		// Get DXGI adapter
		CComPtrCustom<IDXGIAdapter> lDxgiAdapter;
		hr = lDxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&lDxgiAdapter));

		if (FAILED(hr))
		{
			return false;
		}

		lDxgiDevice.Release();

		UINT Output = 0;

		// Get output
		CComPtrCustom<IDXGIOutput> lDxgiOutput;
		hr = lDxgiAdapter->EnumOutputs(
			Output,
			&lDxgiOutput);

		if (FAILED(hr))
		{
			return false;
		}

		lDxgiAdapter.Release();

		hr = lDxgiOutput->GetDesc(
			&lOutputDesc);

		if (FAILED(hr))
		{
			return false;
		}

		// QI for Output 1
		CComPtrCustom<IDXGIOutput1> lDxgiOutput1;

		hr = lDxgiOutput->QueryInterface(IID_PPV_ARGS(&lDxgiOutput1));

		if (FAILED(hr))
		{
			return false;
		}

		lDxgiOutput.Release();

		// Create desktop duplication
		hr = lDxgiOutput1->DuplicateOutput(
			lDevice,
			&lDeskDupl);

		if (FAILED(hr))
		{
			return false;
		}

		lDxgiOutput1.Release();

		// Create GUI drawing texture
		lDeskDupl->GetDesc(&lOutputDuplDesc);

		D3D11_TEXTURE2D_DESC desc;

		desc.Width = lOutputDuplDesc.ModeDesc.Width;

		desc.Height = lOutputDuplDesc.ModeDesc.Height;

		desc.Format = lOutputDuplDesc.ModeDesc.Format;

		desc.ArraySize = 1;

		desc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_RENDER_TARGET;

		desc.MiscFlags = D3D11_RESOURCE_MISC_GDI_COMPATIBLE;

		desc.SampleDesc.Count = 1;

		desc.SampleDesc.Quality = 0;

		desc.MipLevels = 1;

		desc.CPUAccessFlags = 0;

		desc.Usage = D3D11_USAGE_DEFAULT;

		hr = lDevice->CreateTexture2D(&desc, NULL, &lGDIImage);

		if (FAILED(hr))
		{
			return false;
		}

		if (lGDIImage == nullptr)
		{
			return false;
		}

		// Create CPU access texture

		desc.Width = lOutputDuplDesc.ModeDesc.Width;

		desc.Height = lOutputDuplDesc.ModeDesc.Height;

		desc.Format = lOutputDuplDesc.ModeDesc.Format;

		desc.ArraySize = 1;

		desc.BindFlags = 0;

		desc.MiscFlags = 0;

		desc.SampleDesc.Count = 1;

		desc.SampleDesc.Quality = 0;

		desc.MipLevels = 1;

		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
		desc.Usage = D3D11_USAGE_STAGING;

		hr = lDevice->CreateTexture2D(&desc, NULL, &lDestImage);

		if (FAILED(hr))
		{
			return false;
		}

		if (lDestImage == nullptr)
		{
			return false;
		}

		return true;
	}


	bool ScreenCaptureProcessorGDI::GrabImage(UCHAR* &CaptureBuffer, long &CaptureSize)
	{
		char file_name[1024];
		int lTryCount = 4;
		CComPtrCustom<IDXGIResource> lDesktopResource;
		CComPtrCustom<ID3D11Texture2D> lAcquiredDesktopImage;
		DXGI_OUTDUPL_FRAME_INFO lFrameInfo;

		do
		{
			hr = lDeskDupl->AcquireNextFrame(INFINITE, &lFrameInfo, &lDesktopResource);

			if (SUCCEEDED(hr))
				break;

			if (hr == DXGI_ERROR_WAIT_TIMEOUT)
			{
				continue;
			}
			else if (FAILED(hr))
			{
				break;
			}

		} while (--lTryCount > 0);

		if (FAILED(hr))
		{
			error = true;
			return false;
		}

		hr = lDesktopResource->QueryInterface(IID_PPV_ARGS(&lAcquiredDesktopImage));

		lDesktopResource.Release();

		if (FAILED(hr))
		{
			error = true;
			return false;
		}

		if (lAcquiredDesktopImage == nullptr)
		{
			error = true;
			return false;
		}

		// Copy image into GDI drawing texture
		lImmediateContext->CopyResource(lGDIImage, lAcquiredDesktopImage);

		lAcquiredDesktopImage.Release();

		lDeskDupl->ReleaseFrame();

		// Copy image into CPU access texture
		lImmediateContext->CopyResource(lDestImage, lGDIImage);

		// Copy from CPU access texture to bitmap buffer
		D3D11_MAPPED_SUBRESOURCE resource;
		UINT subresource = D3D11CalcSubresource(0, 0, 0);
		lImmediateContext->Map(lDestImage, subresource, D3D11_MAP_READ_WRITE, 0, &resource);

		BITMAPINFO	lBmpInfo;

		// BMP 32 bpp
		ZeroMemory(&lBmpInfo, sizeof(BITMAPINFO));

		lBmpInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

		lBmpInfo.bmiHeader.biBitCount = 32;

		lBmpInfo.bmiHeader.biCompression = BI_RGB;

		lBmpInfo.bmiHeader.biWidth = lOutputDuplDesc.ModeDesc.Width;

		lBmpInfo.bmiHeader.biHeight = lOutputDuplDesc.ModeDesc.Height;

		lBmpInfo.bmiHeader.biPlanes = 1;

		lBmpInfo.bmiHeader.biSizeImage = lOutputDuplDesc.ModeDesc.Width
			* lOutputDuplDesc.ModeDesc.Height * 4;

		std::unique_ptr<BYTE> pBuf(new BYTE[lBmpInfo.bmiHeader.biSizeImage]);

		UINT lBmpRowPitch = lOutputDuplDesc.ModeDesc.Width * 4;

		BYTE* sptr = reinterpret_cast<BYTE*>(resource.pData);
		BYTE* dptr = CaptureBuffer;//pBuf.get() /*+ lBmpInfo.bmiHeader.biSizeImage - lBmpRowPitch*/;

		UINT lRowPitch = std::min<UINT>(lBmpRowPitch, resource.RowPitch);

		
		clock_t start = clock();
		for (size_t h = 0; h < lOutputDuplDesc.ModeDesc.Height; ++h)
		{
			memcpy_s(dptr, lBmpRowPitch, sptr, lRowPitch);
			sptr += resource.RowPitch;
			dptr += lBmpRowPitch;
		}
		clock_t stop = clock();

		clock_t nDiff = stop - start;

		//fprintf(log_file, "average time for scale - %ld\n", nDiff);


		lImmediateContext->Unmap(lDestImage, subresource);

		CaptureSize = lBmpRowPitch;
		/*_snprintf_s(file_name, sizeof(file_name), "Content\\Captured_Image\\%d.bmp", frame_count);
		save_as_bitmap(CaptureBuffer, CaptureSize, lOutputDuplDesc.ModeDesc.Width, lOutputDuplDesc.ModeDesc.Height, file_name);*/
		
		frame_count++;

		return true;
	}

#pragma region Save Image 
	void ScreenCaptureProcessorGDI::save_as_bitmap(unsigned char *bitmap_data, int rowPitch, int width, int height, char *filename)
	{
		// A file is created, this is where we will save the screen capture.

		FILE *f;

		BITMAPFILEHEADER   bmfHeader;
		BITMAPINFOHEADER   bi;

		bi.biSize = sizeof(BITMAPINFOHEADER);
		bi.biWidth = width;
		//Make the size negative if the image is upside down.
		bi.biHeight = -height;
		//There is only one plane in RGB color space where as 3 planes in YUV.
		bi.biPlanes = 1;
		//In windows RGB, 8 bit - depth for each of R, G, B and alpha.
		bi.biBitCount = 32;
		//We are not compressing the image.
		bi.biCompression = BI_RGB;
		// The size, in bytes, of the image. This may be set to zero for BI_RGB bitmaps.
		bi.biSizeImage = 0;
		bi.biXPelsPerMeter = 0;
		bi.biYPelsPerMeter = 0;
		bi.biClrUsed = 0;
		bi.biClrImportant = 0;

		// rowPitch = the size of the row in bytes.
		DWORD dwSizeofImage = rowPitch * height;

		// Add the size of the headers to the size of the bitmap to get the total file size
		DWORD dwSizeofDIB = dwSizeofImage + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

		//Offset to where the actual bitmap bits start.
		bmfHeader.bfOffBits = (DWORD)sizeof(BITMAPFILEHEADER) + (DWORD)sizeof(BITMAPINFOHEADER);

		//Size of the file
		bmfHeader.bfSize = dwSizeofDIB;

		//bfType must always be BM for Bitmaps
		bmfHeader.bfType = 0x4D42; //BM   

								   // TODO: Handle getting current directory
		fopen_s(&f, filename, "wb");

		DWORD dwBytesWritten = 0;
		dwBytesWritten += fwrite(&bmfHeader, sizeof(BITMAPFILEHEADER), 1, f);
		dwBytesWritten += fwrite(&bi, sizeof(BITMAPINFOHEADER), 1, f);
		dwBytesWritten += fwrite(bitmap_data, 1, dwSizeofImage, f);

		fclose(f);
	}
	
#pragma endregion
	bool ScreenCaptureProcessorGDI::release()
	{
		return true;
	}

	void ScreenCaptureProcessorGDI::setMaxFrames(unsigned int maxFrames)
	{
		max_count_frames = maxFrames;
	}

	bool ScreenCaptureProcessorGDI::hasFailed()
	{
		return error == true;
	}





	
