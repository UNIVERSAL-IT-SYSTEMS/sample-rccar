// Controller.cpp : a wrapper for reading HID Controllers

#include "Controller.h"
#include "nesmapping.h"

#include <Hidclass.h>
#include <Hidsdi.h>
#include <setupapi.h>
#include <Usbiodef.h>
#include <devguid.h>

#pragma comment(lib, "Hid")
#pragma comment(lib, "setupapi")

Controller::Controller(){
	//init required variables
	_controllerHandle = nullptr;
	_pCaps = nullptr;
	_pButtonCaps = nullptr;
	_pValueCaps = nullptr;
	_pPreparsedData = 0;
	_pButtonUsages = nullptr;
	_pInputReport = nullptr;
	_succ = 0;
}

Controller::~Controller(){

	if (_pPreparsedData != 0){
		HidD_FreePreparsedData(_pPreparsedData);
	}
	if (_pCaps != nullptr){
		free(_pCaps);
	}
	if (_pButtonCaps != nullptr){
		free(_pButtonCaps);
	}
	if (_pButtonUsages != nullptr){
		free(_pButtonUsages);
	}
	if (_pInputReport != nullptr){
		free(_pInputReport);
	}


}


bool Controller::registerController(int pDeviceId){
	
	//Required variables to iterate over the device interface
	DWORD result = 0;
	LPGUID hidguid = (LPGUID)malloc(sizeof(GUID));
	HDEVINFO deviceset;
	PSP_DEVICE_INTERFACE_DATA deviceInterfaceData = nullptr;
	PSP_DEVICE_INTERFACE_DETAIL_DATA deviceInterfaceInfo = nullptr;
	DWORD buffersize = 0;
	
	//get hid guid
	HidD_GetHidGuid(hidguid);

	//get device list
	deviceset = SetupDiGetClassDevs(hidguid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (deviceset == INVALID_HANDLE_VALUE) goto SETUP_ERROR;

	int deviceindex = 0;
	deviceInterfaceData = (PSP_DEVICE_INTERFACE_DATA)malloc(sizeof(SP_DEVICE_INTERFACE_DATA));
	deviceInterfaceData->cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	//iterate through devices
	while (SetupDiEnumDeviceInterfaces(deviceset, nullptr, hidguid, deviceindex, deviceInterfaceData)){
		//this should fail originally in order to get buffer size
		if (deviceInterfaceInfo != nullptr){
			free(deviceInterfaceInfo);
		}
		if (SetupDiGetDeviceInterfaceDetail(deviceset, deviceInterfaceData, nullptr, 0, (PDWORD)&buffersize, nullptr) ||
			GetLastError() != ERROR_INSUFFICIENT_BUFFER){
			goto SETUP_ERROR;
		}
		deviceInterfaceInfo = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(buffersize);
		deviceInterfaceInfo->cbSize = sizeof(*deviceInterfaceInfo);
		
		//device info buffer allocated, get details
		if (!SetupDiGetDeviceInterfaceDetail(deviceset, deviceInterfaceData, deviceInterfaceInfo, buffersize, (PDWORD)&buffersize, nullptr)){
			goto SETUP_ERROR;
		}
		
		//you must pass in the device id of the hid client to read from
		if (deviceindex == pDeviceId){
			_controllerHandle = CreateFile(
				deviceInterfaceInfo->DevicePath,
				GENERIC_READ | GENERIC_WRITE,
				0,
				nullptr,
				OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL,
				nullptr
				);
			if (_controllerHandle == INVALID_HANDLE_VALUE){
				goto HID_SETUP_FAILURE;
			}
			else{

		    	//get preparsed data
				if (HidD_GetPreparsedData(_controllerHandle, &_pPreparsedData) == FALSE){
					goto HID_SETUP_FAILURE;
				}

				//get device capabilities
				_pCaps = (PHIDP_CAPS)(malloc(sizeof(HIDP_CAPS)));
				_succ = HidP_GetCaps(_pPreparsedData, _pCaps);

				if (_succ == HIDP_STATUS_INVALID_PREPARSED_DATA){
					goto HID_SETUP_FAILURE;
				}

				//get button capabilities
				_pButtonCaps = (PHIDP_BUTTON_CAPS)malloc(sizeof(HIDP_BUTTON_CAPS)* _pCaps->NumberInputButtonCaps);
				USHORT numInputButtonCaps = _pCaps->NumberInputButtonCaps;

				_succ = HidP_GetButtonCaps(HidP_Input, _pButtonCaps, &numInputButtonCaps, _pPreparsedData);

				if (_succ != HIDP_STATUS_SUCCESS){
					goto HID_SETUP_FAILURE;
				}

				//prep the button usage data structs
				_pButtonUsages = (PUSAGE)malloc(sizeof(USAGE)*(_pButtonCaps->Range.DataIndexMax - _pButtonCaps->Range.DataIndexMin + 1));
				ULONG numButtonUsages = _pButtonCaps->Range.UsageMax - _pButtonCaps->Range.UsageMin + 1;

				//get max data length
				_pInputReport = (PCHAR)malloc(_pCaps->InputReportByteLength);
				DWORD readbytecount = 0;

				//get value caps
				_pValueCaps = (PHIDP_VALUE_CAPS)malloc(sizeof(HIDP_VALUE_CAPS)* _pCaps->NumberInputValueCaps);
				USHORT numInputValueCaps = _pCaps->NumberInputValueCaps;

				_succ = HidP_GetValueCaps(HidP_Input, _pValueCaps, &numInputValueCaps, _pPreparsedData);

				if (_succ != HIDP_STATUS_SUCCESS){
					goto HID_SETUP_FAILURE;
				}

				goto SETUP_DONE;


			HID_SETUP_FAILURE:
				if (_pPreparsedData != 0){
					HidD_FreePreparsedData(_pPreparsedData);
				}
				if (_pCaps != nullptr){
					free(_pCaps);
				}
				if (_pButtonCaps != nullptr){
					free(_pButtonCaps);
				}
				if (_pButtonUsages != nullptr){
					free(_pButtonUsages);
				}
				if (_pInputReport != nullptr){
					free(_pInputReport);
				}
				return false;
			}
		}


		deviceindex++;
		deviceInterfaceData->cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
	}

	goto SETUP_DONE;

SETUP_ERROR:
	int err = GetLastError();
	return false;
SETUP_DONE:
	if (deviceset != INVALID_HANDLE_VALUE && deviceset != nullptr){
		SetupDiDestroyDeviceInfoList(deviceset);
	}

	if (hidguid != nullptr){
		free(hidguid);
	}

	if (deviceInterfaceData != nullptr){
		free(deviceInterfaceData);
	}

	if (deviceInterfaceInfo != nullptr){
		free(deviceInterfaceInfo);
	}


	return true;
}


/**
	This function assumes that there are 2 values that can be read from the HID device which represent the DPad's state.
	This will not be true for all HID Devices. If you want to use this function, you must modify it for the correct value count.
	Furthermore, you need to update nesmapping.h, or include a different mapping file, which holds constants for the button mapping.
	(You'll have to figure out the button mapping experimentally using log statements or breakpoints)
 **/
int Controller::getDPadInput(bool * pValues){
	DWORD readbytecount = 0;
	_succ = ReadFile(
		_controllerHandle,
		_pInputReport,
		_pCaps->InputReportByteLength,
		&readbytecount,
		nullptr
		);

	if (!_succ){
		goto DPAD_INPUT_FAILURE;
	}

	//use the input report to generate button usage data
	LONG value = 0;

	//reset direction flags
	for (int i = 0; i < NUM_DIRECTIONS; i++){
		pValues[i] = false;
	}

	for (int valIt = 0; valIt < _pCaps->NumberInputValueCaps; valIt++){
		HidP_GetUsageValue(HidP_Input, _pValueCaps[valIt].UsagePage, _pValueCaps[valIt].LinkCollection, _pValueCaps[valIt].Range.UsageMin, (PULONG)&value, _pPreparsedData,
			_pInputReport, readbytecount
			);

		if (valIt == 0){
			switch (value){
			case DPAD_UP:
				pValues[DPAD_UP_INDEX] = true;
				break;
			case DPAD_DOWN:
				pValues[DPAD_DOWN_INDEX] = true;
				break;
			default:
				break;
			}
		}
		else{
			switch (value){
			case DPAD_RIGHT:
				pValues[DPAD_RIGHT_INDEX] = true;
				break;
			case DPAD_LEFT:
				pValues[DPAD_LEFT_INDEX] = true;
				break;
			default:
				break;
			}
		}

	}

	return _pCaps->NumberInputValueCaps;

DPAD_INPUT_FAILURE:
		if (_pPreparsedData != 0){
			HidD_FreePreparsedData(_pPreparsedData);
		}
		if (_pCaps != nullptr){
			free(_pCaps);
		}
		if (_pButtonCaps != nullptr){
			free(_pButtonCaps);
		}
		if (_pButtonUsages != nullptr){
			free(_pButtonUsages);
		}
		if (_pInputReport != nullptr){
			free(_pInputReport);
		}
		return -1;

}

/**
This function assumes that there are 4 buttons that can be read from the HID device which represent the button's usages values.
This will not be true for all HID Devices. If you want to use this function, you must modify it for the correct number of buttons.
Furthermore, you need to update nesmapping.h, or include a different mapping file, which holds constants for the button mapping.
(You'll have to figure out the button mapping experimentally using log statements or breakpoints)
**/
int Controller::getButtonInput(bool * pValues){
	DWORD readbytecount = 0;
	_succ = ReadFile(
		_controllerHandle,
		_pInputReport,
		_pCaps->InputReportByteLength,
		&readbytecount,
		nullptr
		);

	if (!_succ){
		goto DPAD_INPUT_FAILURE;
	}

	ULONG numButtonUsages = _pButtonCaps->Range.UsageMax - _pButtonCaps->Range.UsageMin + 1;

	//use the input report to generate button usage data
	HidP_GetUsages(HidP_Input, _pButtonCaps[0].UsagePage, _pButtonCaps[0].LinkCollection, _pButtonUsages, &numButtonUsages, _pPreparsedData, _pInputReport, _pCaps->InputReportByteLength);
	
	//reset button values to 0
	for (unsigned int i = 0; i < NUM_BUTTONS; i++){
		pValues[i] = false;
	}

	//every button that is currently pressed will have it's ID in the _pButtonUsages array
	for (unsigned int i = 0; i < numButtonUsages; i++){
		USAGE dat = _pButtonUsages[i];
		switch (dat){
		case BUTTON_A:
			pValues[BUTTON_A_INDEX] = true;
				break;
		case BUTTON_B:
			pValues[BUTTON_B_INDEX] = true;
				break;
		case BUTTON_SELECT:
			pValues[BUTTON_SELECT_INDEX] = true;
				break;
		case BUTTON_START:
			pValues[BUTTON_START_INDEX] = true;
				break;
			default:
			break;
		}
	}
	return numButtonUsages;



DPAD_INPUT_FAILURE:
	if (_pPreparsedData != 0){
		HidD_FreePreparsedData(_pPreparsedData);
	}
	if (_pCaps != nullptr){
		free(_pCaps);
	}
	if (_pButtonCaps != nullptr){
		free(_pButtonCaps);
	}
	if (_pButtonUsages != nullptr){
		free(_pButtonUsages);
	}
	if (_pInputReport != nullptr){
		free(_pInputReport);
	}
	return -1;

}

