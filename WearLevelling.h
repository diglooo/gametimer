/*
Davide Di Gloria - davidedigloria87@gmail.com
May 2020

Byte-level Wear levelling utility for AVR's EEPROM.

Works by looking for a fixed 32bit header in eeprom data upon power up.
After each write operation, the base address is incremented by 1 byte, this will shift the stored data by 1 byte each time.
*/

#include <EEPROM.h>
#include <util/crc16.h>

#define WL_START_HEADER (uint32_t)0xA5AFFA5A

class E2WearLevelling
{
private:
	uint16_t currentBaseAddress = 0;
	uint8_t initOk = 0;

public:
	uint16_t getStartAddress() { return currentBaseAddress; }
	uint8_t isInitialized() { return initOk; }

	/*
	Call this on device reset or powerup.
	The whole eeprom is scanned to look for the star header.
	Once the header is found, its addess is saved and used for next write operations.
	*/
	void init()
	{
		uint32_t readVal = 0;
		currentBaseAddress = 0;
		//scan EEPROM byte by byte looking for WL_START_HEADER
		for (uint16_t i = 0; i < E2END; i++)
		{
			readVal = eeprom_read_dword((uint32_t *)i);
			if (readVal == WL_START_HEADER)
			{
				currentBaseAddress = i;
				break;
			}
		}
		initOk = 1;
	}

	void format()
	{
		for (uint16_t i = 0; i < E2END; i++)
			eeprom_write_byte((uint8_t *)i, 0);
		eeprom_write_dword(0, WL_START_HEADER);
	}

	uint8_t writeToEEPROM(void* _data, uint16_t _dataSize)
	{
		uint16_t checksum = 0;
		if (!initOk) return 0;

		//increment starting address (wear levelling happens here)
		currentBaseAddress++;

		//if end address is bigger than EEPROM size, roll back to 0
		if (currentBaseAddress + _dataSize + sizeof(WL_START_HEADER) + sizeof(checksum) >= E2END)
			currentBaseAddress = 0;

		//write 4 bytes start pattern 
		eeprom_write_dword((uint32_t*)currentBaseAddress, WL_START_HEADER);

		//write 2 byte 16 bit checksum of data
		checksum = calcCrc16(_data, _dataSize);
		eeprom_write_word((uint16_t*)(currentBaseAddress + sizeof(WL_START_HEADER)), checksum);

		//write actual data
		uint8_t *dataPtr = (uint8_t *)_data;
		for (int i = sizeof(WL_START_HEADER) + sizeof(uint16_t); i < _dataSize + sizeof(WL_START_HEADER) + sizeof(uint16_t); i++)
			eeprom_write_byte((uint8_t *)(currentBaseAddress + i), *dataPtr++);

		return 1;
	}

	uint8_t readFromEEPROM(void* _data, uint16_t _dataSize)
	{
		if (!initOk) return 0;

		//check data stored in EEPROM
		if (checkDataOnEEPROM(_dataSize))
		{
			//read data from EEPROM
			uint8_t *dataPtr = (uint8_t *)_data;
			for (int i = sizeof(WL_START_HEADER) + sizeof(uint16_t); i < _dataSize + sizeof(WL_START_HEADER) + sizeof(uint16_t); i++)
				*dataPtr++ = eeprom_read_byte((uint8_t *)(i + currentBaseAddress));
			return 1;
		}
		else return 0;
	}

	//calculate and compare CRC16 on EEPROM data
	uint8_t checkDataOnEEPROM(uint16_t _dataSize)
	{
		if (!initOk) return 0;

		//read checksum stored on eeprom
		uint16_t storedChecksum = eeprom_read_word((uint16_t*)(currentBaseAddress + sizeof(WL_START_HEADER)));
		uint16_t dataChecksum = 0;

		//calculate checksum on stored data
		for (int i = sizeof(WL_START_HEADER) + sizeof(uint16_t); i < _dataSize + sizeof(WL_START_HEADER) + sizeof(uint16_t); i++)
		{
			uint8_t temp = eeprom_read_byte((uint8_t *)(i + currentBaseAddress));
			dataChecksum = _crc16_update(dataChecksum, temp);
		}
		return(dataChecksum == storedChecksum);
	}

	//calc CRC16 on SRAM data
	uint16_t calcCrc16(void* _data, uint16_t _dataSize)
	{
		uint16_t checksum = 0;
		uint8_t *dataPtr = (uint8_t *)_data;
		for (int i = 0; i < _dataSize; i++)
			checksum = _crc16_update(checksum, *dataPtr++);
		return checksum;
	}
};
