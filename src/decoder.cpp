#include <iostream>
#include <fstream>

#include "jpeg.h"

void readStartOfFrame(std::ifstream &inFile, Header *const header)
{
	std::cout << "Reading SOF Marker\n";
	if (header->numComponents.to_ulong() != 0)
	{
		std::cout << "Error - Multiple SOFs detected\n";
		header->valid = false;
		return;
	}

	uint length = (inFile.get() << 8) + inFile.get();

	byte precision = inFile.get();
	if (precision.to_ulong() != 8)
	{
		std::cout << "Error - Invalid precision: " << precision.to_ulong() << "\n";
		header->valid = false;
		return;
	}

	header->height = (inFile.get() << 8) + inFile.get();
	header->width = (inFile.get() << 8) + inFile.get();
	if (header->height == 0 || header->width == 0)
	{
		std::cout << "Error - Invalid dimension\n";
	}

	header->numComponents = inFile.get();
	if (header->numComponents.to_ulong() == 4)
	{
		std::cout << "Error - CMYK color mode not supported\n";
		header->valid = false;
		return;
	}
	if (header->numComponents.to_ulong() == 0)
	{
		std::cout << "Error - Number of color components must not be 0\n";
		header->valid = false;
		return;
	}

	for (uint i = 0; i < header->numComponents.to_ulong(); ++i)
	{
		byte componentID = inFile.get();
		if (componentID.to_ulong() == 4 || componentID.to_ulong() == 5)
		{
			std::cout << "Error - YIQ color mode not supported\n";
			header->valid = false;
			return;
		}
		if (componentID.to_ulong() == 0 || componentID.to_ulong() > 3)
		{
			std::cout << "Error - Invalid component ID: " << componentID.to_ulong() << "\n";
			header->valid = false;
			return;
		}
		ColorComponent *component = &header->colorComponents[componentID.to_ulong() - 1];
		if (component->used)
		{
			std::cout << "Error - Duplicate color component ID\n";
			header->valid = false;
			return;
		}
		component->used = true;
		byte SamplingFactor = inFile.get();
		component->horizontalSamplingFactor = SamplingFactor.to_ulong() >> 4; //First four bits has the horizontal sampling factor.
		component->verticalSamplingFactor = SamplingFactor.to_ulong() & 0x0F; //Last four bits has the vertical sampling factor.
		if (component->horizontalSamplingFactor != 1 || component->verticalSamplingFactor != 1)
		{
			std::cout << "Error - Sampling factors not supported\n";
			header->valid = false;
			return;
		}

		component->quantizationTableID = inFile.get();
		if (component->quantizationTableID.to_ulong() > 3)
		{
			std::cout << "Error - Invalid quantization table ID in frame component\n";
			header->valid = false;
			return;
		}
	}
	if (length - 8 - (3 * header->numComponents.to_ulong()) != 0)
	{
		std::cout << "Error - SOF invalid\n";
		header->valid = false;
		return;
	}
}

void readQuantizationTable(std::ifstream &inFile, Header *const header)
{
	std::cout << "Reading DQT marker\n";

	/* 
	The reason why we are using a signed int is so that the while condition (length > 0) doesn't break. An unsigned int
	will always be greater than 0 so we will be stuck in an infinte while loop.
	 */
	int length = (inFile.get() << 8) + inFile.get(); //Left bit shift since JPEG is read in big endian. Convert it to little endian.
	length -= 2;

	while (length > 0) //Using while here instead of a for loop. This is because the DQT marker could hold more than one Quantization Table, in which case, the length can vary in size.
	{
		byte tableInfo = inFile.get();
		length -= 1;
		byte tableID = tableInfo.to_ulong() & 0x0F; //Read the last four bits of the tableInfo byte. This will hold a value between 0-3.

		if (tableID.to_ullong() > 3)
		{
			std::cout << "Error - Invalid quantization table ID: " << (uint)tableID.to_ulong() << "\n";
			header->valid = false;
			return;
		}
		header->quantizationTables[tableID.to_ullong()].set = true;

		if (tableInfo.to_ulong() >> 4 != 0) //Bit shift so that we are only reading the first four bits.
		{
			//This is to handle Quantization Table with 16 bit values.
			for (uint i = 0; i < 64; ++i)
			{
				header->quantizationTables[tableID.to_ulong()].table[zigZagMap[i]] = (inFile.get() << 8) + inFile.get();
			}
			length -= 128;
		}
		else
		{
			//This for 8 bit values.
			for (uint i = 0; i < 64; ++i)
			{
				header->quantizationTables[tableID.to_ulong()].table[zigZagMap[i]] = inFile.get();
			}
			length -= 64;
		}
		/* 
		The length -= XX is so that it resolves to either a value of 0 or lower which will make it break out of the loop.
		If it's lower than there was an issue.
		 */
	}

	if (length != 0)
	{
		std::cout << "Error - DQT invalid\n";
		header->valid = false;
	}
}

void readAPPN(std::ifstream &inFile, Header *const header)
{
	std::cout << "Reading APPN Marker\n";
	uint length = (inFile.get() << 8) + inFile.get(); //Left bit shift since JPEG is read in big endian. Convert it to little endian.

	for (uint i = 0; i < length - 2; ++i)
	{
		inFile.get();
	}
}

Header *readJPG(const std::string &filename)
{
	//Open file
	std::ifstream inFile = std::ifstream(filename, std::ios::in | std::ios::binary);
	if (!inFile.is_open())
	{
		std::cout << "Error - Error opening input file\n";
		return nullptr;
	}

	Header *header = new (std::nothrow) Header;
	if (header == nullptr)
	{
		std::cout << "Error - Memory error\n";
		inFile.close();
		return nullptr;
	}

	byte last = inFile.get();
	byte current = inFile.get();
	if (last != 0xFF || current.to_ulong() != SOI.to_ullong())
	{
		header->valid = false;
		inFile.close();
		return header;
	}

	last = inFile.get();
	current = inFile.get();
	while (header->valid)
	{
		if (!inFile)
		{
			std::cout << "Error - File ended prematurely\n";
			header->valid = false;
			inFile.close();
			return header;
		}
		while (last != 0xFF && current != 0xDB)
		{
			last = inFile.get();
			current = inFile.get();
		}

		if (last != 0xFF)
		{
			std::cout << "Error - Expected a marker\n";
			header->valid = false;
			inFile.close();
			return header;
		}
		if (current == SOF0)
		{
			header->frameType = SOF0;
			readStartOfFrame(inFile, header);
			break;
		}

		if (current == DQT)
		{
			readQuantizationTable(inFile, header);
		}
		if (current.to_ulong() >= APP0.to_ulong() && current.to_ulong() <= APP15.to_ulong())
		{
			readAPPN(inFile, header);
		}

		last = inFile.get();
		current = inFile.get();
	}
	return header;
}

void printHeader(const Header *const header)
{
	if (header == nullptr)
		return;
	std::cout << "DQT============\n";
	for (uint i = 0; i < 4; ++i)
	{
		if (header->quantizationTables[i].set)
		{
			std::cout << "Table ID: " << i << "\n";
			std::cout << "Table Data:";
			for (uint j = 0; j < 64; ++j)
			{
				if (j % 8 == 0)
				{
					std::cout << "\n";
				}
				std::cout << header->quantizationTables[i].table[j] << " ";
			}
			std::cout << "\n";
		}
	}
	std::cout << "SOF============\n";
	std::cout << "Frame Type: 0x" << std::hex << header->frameType.to_ulong() << std::dec << "\n";
	std::cout << "Height: " << header->height << "\n";
	std::cout << "Width: " << header->width << "\n";
	for (uint i = 0; i < header->numComponents.to_ulong(); ++i)
	{
		std::cout << "Component ID: " << (i + 1) << "\n";
		std::cout << "Horizontal Sampling Factor: " << header->colorComponents[i].horizontalSamplingFactor.to_ulong() << "\n";
		std::cout << "Vertical Sampling Factor: " << header->colorComponents[i].verticalSamplingFactor.to_ulong() << "\n";
		std::cout << "Quantization Table ID: " << header->colorComponents[i].quantizationTableID.to_ulong() << "\n";
	}
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		std::cout << "Error - Invalid arguments\n";
		return 1;
	}
	for (int i = 1; i < argc; ++i)
	{
		const std::string filename(argv[i]);
		Header *header = readJPG(filename);
		if (header == nullptr)
		{
			continue;
		}
		if (header->valid == false)
		{
			std::cout << "Error - Invalid JPG\n";
			delete header;
			continue;
		}

		printHeader(header);

		//Decode Huffman data.

		delete header;
	}
	std::cout << "\nWritten by Digit\n";
	return 0;
}