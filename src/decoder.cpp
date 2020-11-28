#include <fstream>
#include <iostream>

#include "jpeg.h"

void readStartOfFrame(std::ifstream& inFile, Header* const header)
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
        if (componentID == 0)
        {
            header->zeroBased = true;
        }
        if (header->zeroBased)
        {
            componentID = componentID.to_ulong() + 1;
        }
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
        ColorComponent* component = &header->colorComponents[componentID.to_ulong() - 1];
        if (component->used)
        {
            std::cout << "Error - Duplicate color component ID\n";
            header->valid = false;
            return;
        }
        component->used = true;
        byte SamplingFactor = inFile.get();
        component->horizontalSamplingFactor = SamplingFactor.to_ulong()
                                              >> 4; // First four bits has the horizontal sampling
                                                    // factor.
        component->verticalSamplingFactor = SamplingFactor.to_ulong()
                                            & 0x0F; // Last four bits has the vertical sampling
                                                    // factor.
        // if (component->horizontalSamplingFactor.to_ulong() != 1 ||
        // component->verticalSamplingFactor.to_ulong() != 1)
        // {
        // 	std::cout << "Error - Sampling factors not supported\n";
        // 	std::cout << "Horizontal Sampling Factor: " <<
        // component->horizontalSamplingFactor.to_ulong() << "\n"; 	std::cout <<
        // "Vertical Sampling Facotr: " <<
        // component->verticalSamplingFactor.to_ulong() << "\n"; 	header->valid =
        // false; 	return;
        // }

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

void readQuantizationTable(std::ifstream& inFile, Header* const header)
{
    std::cout << "Reading DQT marker\n";

    /*
  The reason why we are using a signed int is so that the while condition
  (length > 0) doesn't break. An unsigned int will always be greater than 0,
  therefore an infinte while loop.
   */
    int length = (inFile.get() << 8)
                 + inFile.get(); // Left bit shift since JPEG is read in big endian.
    length -= 2;

    while (length > 0) // Using while here instead of a for loop. This is because
                       // the DQT marker could hold more than one Quantization
                       // Table, in which case, the length can vary in size.
    {
        byte tableInfo = inFile.get();
        length -= 1;
        byte tableID = tableInfo.to_ulong() & 0x0F; // Read the last four bits of the tableInfo
                                                    // byte. This will hold a value between 0-3.

        if (tableID.to_ullong() > 3)
        {
            std::cout << "Error - Invalid quantization table ID: " << (uint)tableID.to_ulong()
                      << "\n";
            header->valid = false;
            return;
        }
        header->quantizationTables[tableID.to_ullong()].set = true;

        if (tableInfo.to_ulong() >> 4
            != 0) // Bit shift so that we are only reading the first four bits.
        {
            // This is to handle Quantization Table with 16 bit values.
            for (uint i = 0; i < 64; ++i)
            {
                header->quantizationTables[tableID.to_ulong()].table[zigZagMap[i]] = (inFile.get()
                                                                                      << 8)
                                                                                     + inFile.get();
            }
            length -= 128;
        }
        else
        {
            // This for 8 bit values.
            for (uint i = 0; i < 64; ++i)
            {
                header->quantizationTables[tableID.to_ulong()].table[zigZagMap[i]] = inFile.get();
            }
            length -= 64;
        }
        /*
    The length -= XX is so that it resolves to either a value of 0 or lower
    which will make it break out of the loop. If it's lower than there was an
    issue.
     */
    }

    if (length != 0)
    {
        std::cout << "Error - DQT invalid\n";
        header->valid = false;
    }
}

void readHuffmanTable(std::ifstream& inFile, Header* const header)
{
    std::cout << "Reading DHT Marker\n";
    int length = (inFile.get() << 8)
                 + inFile.get(); // Left bit shift since JPEG is read in big endian.
    length -= 2;

    while (length > 0)
    {
        byte tableInfo = inFile.get();
        byte tableID = tableInfo.to_ulong() & 0x0F;
        bool ACTable = tableInfo.to_ulong() >> 4;

        if (tableID.to_ulong() > 3)
        {
            std::cout << "Error - Invalid Huffman table ID: " << tableID.to_ulong() << "\n";
            header->valid = false;
            return;
        }

        HuffmanTable* hTable;
        if (ACTable)
        {
            hTable = &header->huffmanACTables[tableID.to_ulong()];
        }
        else
        {
            hTable = &header->huffmanDCTables[tableID.to_ulong()];
        }
        hTable->set = true;

        hTable->offsets[0] = 0;
        uint allSymbols = 0;
        for (uint i = 1; i <= 16; ++i)
        {
            allSymbols += inFile.get();
            hTable->offsets[i] = allSymbols;
        }
        if (allSymbols > 162)
        {
            std::cout << "Error - Too many symbols in Huffman table\n";
            header->valid = false;
            return;
        }

        for (uint i = 0; i < allSymbols; ++i)
        {
            hTable->symbols[i] = inFile.get();
        }

        length -= 17 + allSymbols;
    }
    if (length != 0)
    {
        std::cout << "Error - DHT invalid\n";
        header->valid = false;
    }
}

void readStartOfScan(std::ifstream& inFile, Header* const header)
{
    std::cout << "Reading SOS Marker\n";
    if (header->numComponents == 0)
    {
        std::cout << "Error - SOS detected before SOF\n";
        header->valid = false;
        return;
    }

    uint length = (inFile.get() << 8) + inFile.get();

    for (uint i = 0; i < header->numComponents.to_ulong(); ++i)
    {
        header->colorComponents[i].used = false;
    }

    byte numComponents = inFile.get();
    for (uint i = 0; i < numComponents.to_ulong(); ++i)
    {
        byte componentID = inFile.get();
        if (header->zeroBased)
        {
            componentID = componentID.to_ulong() + 1;
        }
        if (componentID.to_ulong() > header->numComponents.to_ulong())
        {
            std::cout << "Error - Invalid color compoentID: " << componentID.to_ulong() << "\n";
            header->valid = false;
            return;
        }
        ColorComponent* component = &header->colorComponents[componentID.to_ulong() - 1];
        if (component->used)
        {
            std::cout << "Error - Duplicate color component ID: " << componentID.to_ulong() << "\n";
            header->valid = false;
            return;
        }
        component->used = true;

        byte huffmanTableIDs = inFile.get();
        component->huffmanDCTableID = huffmanTableIDs.to_ulong() >> 4;
        component->huffmanACTableID = huffmanTableIDs.to_ulong() & 0x0F;
        if (component->huffmanDCTableID.to_ulong() > 3)
        {
            std::cout << "Error - Huffman DC rable ID: " << component->huffmanDCTableID.to_ulong()
                      << "\n";
            header->valid = false;
            return;
        }
        if (component->huffmanACTableID.to_ulong() > 3)
        {
            std::cout << "Error - Huffman AC rable ID: " << component->huffmanACTableID.to_ulong()
                      << "\n";
            header->valid = false;
            return;
        }
    }
    header->startOfSelection = inFile.get();
    header->endOfSelection = inFile.get();
    byte successiveApproximation = inFile.get();
    header->successiveApproximationHigh = successiveApproximation.to_ulong() >> 4;
    header->successiveApproximationLow = successiveApproximation.to_ulong() & 0x0F;

    // Baseline JPEGs don't use spectral selection or successive approximation
    if (header->startOfSelection.to_ulong() != 0 || header->endOfSelection.to_ulong() != 63)
    {
        std::cout << "Error - Invalid spectral selection\n";
        header->valid = false;
        return;
    }
    if (header->successiveApproximationHigh.to_ulong() != 0
        || header->successiveApproximationLow.to_ulong() != 0)
    {
        std::cout << "Error - Invalid successive approximation\n";
        header->valid = false;
        return;
    }

    if (length - 6 - (2 * numComponents.to_ulong()) != 0)
    {
        std::cout << "Error - SOS invalid\n";
        header->valid = false;
    }
}

void readRestartInterval(std::ifstream& inFile, Header* const header)
{
    std::cout << "Reading DRI Marker\n";
    uint length = (inFile.get() << 8)
                  + inFile.get(); // Left bit shift since JPEG is read in big endian.

    header->restartInterval = (inFile.get() << 8)
                              + inFile.get(); // Left bit shift since JPEG is read in big endian.

    if (length - 4 != 0)
    {
        std::cout << "Error - DRI invalid\n";
        header->valid = false;
    }
}

void readAPPN(std::ifstream& inFile, Header* const header)
{
    std::cout << "Reading APPN Marker\n";
    uint length = (inFile.get() << 8)
                  + inFile.get(); // Left bit shift since JPEG is read in big endian.

    for (uint i = 0; i < length - 2; ++i)
    {
        inFile.get();
    }
}

void readComment(std::ifstream& inFile, Header* const header)
{
    std::cout << "Reading COM Marker\n";
    uint length = (inFile.get() << 8)
                  + inFile.get(); // Left bit shift since JPEG is read in big endian.

    for (uint i = 0; i < length - 2; ++i)
    {
        inFile.get();
    }
}

Header* readJPG(const std::string& filename)
{
    // Open file
    std::ifstream inFile = std::ifstream(filename, std::ios::in | std::ios::binary);
    if (!inFile.is_open())
    {
        std::cout << "Error - Error opening input file\n";
        return nullptr;
    }

    Header* header = new (std::nothrow) Header;
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
        }
        else if (current == DQT)
        {
            readQuantizationTable(inFile, header);
        }
        else if (current == DHT)
        {
            readHuffmanTable(inFile, header);
        }
        else if (current == SOS)
        {
            readStartOfScan(inFile, header);
            break;
        }
        else if (current == DRI)
        {
            readRestartInterval(inFile, header);
        }
        else if (current.to_ulong() >= APP0.to_ulong() && current.to_ulong() <= APP15.to_ulong())
        {
            readAPPN(inFile, header);
        }
        else if (current == COM)
        {
            readComment(inFile, header);
        }
        // Unused markers that can be skipped
        else if (current.to_ulong() >= JPG0.to_ulong() && current.to_ulong() <= JPG13.to_ulong()
                 || current == DNL || current == DHP || current == EXP)
        {
            readComment(inFile, header);
        }
        else if (current == TEM)
        {
            // TEM has no size
        }
        // Any number of 0xFF in a  row is allowed and should be ignored
        else if (current == 0xFF)
        {
            current == inFile.get();
            continue;
        }
        else if (current == SOI)
        {
            std::cout << "Error - Embedded JPGs not supported\n";
            header->valid = false;
            inFile.close();
            return header;
        }
        else if (current == EOI)
        {
            std::cout << "Error - EOI detected before SOS\n";
            header->valid = false;
            inFile.close();
            return header;
        }
        else if (current == DAC)
        {
            std::cout << "Error - Arithmetic Coding mode not supported\n";
            header->valid = false;
            inFile.close();
            return header;
        }
        else if (current.to_ulong() >= SOF0.to_ulong() && current.to_ulong() <= SOF15.to_ulong())
        {
            std::cout << "Error - SOF marker not supported: 0x" << std::hex << current.to_ulong()
                      << std::dec << "\n";
            header->valid = false;
            inFile.close();
            return header;
        }
        else if (current.to_ulong() >= RST0.to_ulong() && current.to_ulong() <= RST7.to_ulong())
        {
            std::cout << "Error - RSTN detected before SOS\n";
            header->valid = false;
            inFile.close();
            return header;
        }
        else
        {
            std::cout << "Error - Unknown marker: 0x" << std::hex << current.to_ulong() << std::dec
                      << "\n";
            header->valid = false;
            inFile.close();
            return header;
        }

        last = inFile.get();
        current = inFile.get();
    }
    // After SOS
    if (header->valid)
    {
        current = inFile.get();
        // Read compressed image data
        while (true)
        {
            if (!inFile)
            {
                std::cout << "Error - File ended prematurely\n";
                header->valid = false;
                inFile.close();
                return header;
            }

            last = current;
            current = inFile.get();
            // If marker is found
            if (last == 0xFF)
            {
                // End of Image
                if (current == EOI)
                {
                    break;
                }
                // 0xFF 0x00 means put a literal 0xFF in image data and ignore 0x00
                else if (current == 0x00)
                {
                    header->huffmanData.push_back(last);
                    current = inFile.get();
                }
                // If current happens to be a restart marker
                else if (current.to_ulong() >= RST0.to_ulong()
                         && current.to_ulong() <= RST7.to_ulong())
                {
                    // Overwrite marker with next byte
                    current = inFile.get();
                }
                // Ignore multiple 0xFF's in a row
                else if (current == 0xFF)
                {
                    // Do nothing
                    continue;
                }
                else
                {
                    std::cout << "Error - Invalid marker during compressed data scan: 0x"
                              << std::hex << current.to_ulong() << std::dec << "|n";
                    header->valid = false;
                    inFile.close();
                    return header;
                }
            }
            else
            {
                header->huffmanData.push_back(last);
            }
        }
    }

    // Validate header info
    if (header->numComponents.to_ulong() != 1 && header->numComponents.to_ulong() != 3)
    {
        std::cout << "Error - " << header->numComponents.to_ulong()
                  << "color components given (1 or 3 required)"
                  << "\n";
        header->valid = false;
        inFile.close();
        return header;
    }

    for (uint i = 0; i < header->numComponents.to_ulong(); ++i)
    {
        if (header->quantizationTables[header->colorComponents[i].quantizationTableID.to_ulong()].set
            == false)
        {
            std::cout << "Error - Color component using uninitialized quantization table\n";
            header->valid = false;
            inFile.close();
            return header;
        }
        if (header->huffmanDCTables[header->colorComponents[i].huffmanDCTableID.to_ulong()].set
            == false)
        {
            std::cout << "Error - Color component using uninitialized Huffman DC table\n";
            header->valid = false;
            inFile.close();
            return header;
        }
        if (header->huffmanACTables[header->colorComponents[i].huffmanACTableID.to_ulong()].set
            == false)
        {
            std::cout << "Error - Color component using uninitialized Huffman AC table\n";
            header->valid = false;
            inFile.close();
            return header;
        }
    }

    inFile.close();
    return header;
}

void printHeader(const Header* const header)
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
        std::cout << "Horizontal Sampling Factor: "
                  << header->colorComponents[i].horizontalSamplingFactor.to_ulong() << "\n";
        std::cout << "Vertical Sampling Factor: "
                  << header->colorComponents[i].verticalSamplingFactor.to_ulong() << "\n";
        std::cout << "Quantization Table ID: "
                  << header->colorComponents[i].quantizationTableID.to_ulong() << "\n";
    }
    std::cout << "DHT============\n";
    std::cout << "DC tables:\n";
    for (uint i = 0; i < 4; i++)
    {
        if (header->huffmanDCTables[i].set)
        {
            std::cout << "Table ID: " << i << "\n";
            std::cout << "Symbols:\n";
            for (uint j = 0; j < 16; ++j)
            {
                std::cout << (j + 1) << ": ";
                for (uint k = header->huffmanDCTables[i].offsets[j].to_ulong();
                     k < header->huffmanDCTables[i].offsets[j + 1].to_ulong();
                     ++k)
                {
                    std::cout << std::hex << header->huffmanDCTables[i].symbols[k].to_ulong()
                              << std::dec << " ";
                }
                std::cout << "\n";
            }
        }
    }

    std::cout << "AC tables:\n";
    for (uint i = 0; i < 4; i++)
    {
        if (header->huffmanACTables[i].set)
        {
            std::cout << "Table ID: " << i << "\n";
            std::cout << "Symbols:\n";
            for (uint j = 0; j < 16; ++j)
            {
                std::cout << (j + 1) << ": ";
                for (uint k = header->huffmanACTables[i].offsets[j].to_ulong();
                     k < header->huffmanACTables[i].offsets[j + 1].to_ulong();
                     ++k)
                {
                    std::cout << std::hex << header->huffmanACTables[i].symbols[k].to_ulong()
                              << std::dec << " ";
                }
                std::cout << "\n";
            }
        }
    }
    std::cout << "SOS============\n";
    std::cout << "Start of Selection: " << header->startOfSelection.to_ulong() << "\n";
    std::cout << "End of Selection: " << header->endOfSelection.to_ulong() << "\n";
    std::cout << "Successive Approximation High: " << header->successiveApproximationHigh.to_ulong()
              << "\n";
    std::cout << "Successive Approximation Low: " << header->successiveApproximationLow.to_ulong()
              << "\n";
    std::cout << "Color components:\n";
    for (uint i = 0; i < header->numComponents.to_ulong(); ++i)
    {
        std::cout << "Component ID: " << (i + 1) << "\n";
        std::cout << "Huffman DC Table ID: "
                  << header->colorComponents[i].huffmanDCTableID.to_ulong() << "\n";
        std::cout << "Huffman AC Table ID: "
                  << header->colorComponents[i].huffmanACTableID.to_ulong() << "\n";
    }
    std::cout << "Size of Huffman Data: " << header->huffmanData.size() << " Bytes\n";
    std::cout << "DRI============\n";
    std::cout << "Restart Interval: " << header->restartInterval << "\n";
}

void generateCodes(HuffmanTable& hTable) // Generate all Huffman codes based on symbols from a
                                         // Huffman table.
{
    uint code = 0;
    for (uint i = 0; i < 16; ++i)
    {
        for (uint j = hTable.offsets[i].to_ulong(); j < hTable.offsets[i + 1].to_ulong(); ++j)
        {
            hTable.codes[j] = code;
            code += 1;
        }
        code <<= 1;
    }
}

// Helper class to read bits from a byte vector.
class BitReader
{
private:
    uint nextByte = 0;
    uint nextBit = 0;
    const std::vector<byte>& data;

public:
    BitReader(const std::vector<byte>& d) : data(d)
    {
    }

    // Read one bit(0 or 1) or return -1 if all bits have already beeen read.
    int readBit()
    {
        if (nextByte >= data.size())
        {
            return -1;
        }
        int bit = (data[nextByte].to_ulong() >> (7 - nextBit)) & 1;
        nextBit += 1;
        if (nextBit == 8)
        {
            nextBit = 0;
            nextByte += 1;
        }
        return bit;
    }

    int readBits(const uint length)
    {
        int bits = 0;
        for (uint i = 0; i < length; ++i)
        {
            int bit = readBit();
            if (bit == -1)
            {
                bits = -1;
                break;
            }
            bits = (bits << 1) | bit;
        }
        return bits;
    }

    // If there are bits remaining, advance to the 0th bit of the next byte.
    void align()
    {
        if (nextByte >= data.size())
        {
            return;
        }
        if (nextBit != 0)
        {
            nextBit = 0;
            nextByte += 1;
        }
    }
};

// Return the symbol from the Huffman table that corresponds to the next Huffman code read from the
// BitReader.
byte getNextSymbol(BitReader& b, const HuffmanTable& hTable)
{
    uint currentCode = 0;
    for (uint i = 0; i < 16; ++i)
    {
        int bit = b.readBit();
        if (bit == -1)
        {
            return -1;
        }
        currentCode = (currentCode << 1) | bit;
        for (uint j = hTable.offsets[i].to_ulong(); j < hTable.offsets[i + 1].to_ulong(); ++j)
        {
            if (currentCode == hTable.codes[j])
            {
                return hTable.symbols[j].to_ulong();
            }
        }
    }
    return -1;
}

// Fill the coefficient of an MCU component based on Huffman codes read from the BitReader.
bool decodeMCUComponent(BitReader& b,
                        int* const component,
                        int& previousDC,
                        const HuffmanTable& dcTable,
                        const HuffmanTable& acTable)
{
    byte length = getNextSymbol(b, dcTable).to_ulong(); // Get the DC Value for this MCU Component.
    if (length.to_ulong() == -1)
    {
        std::cout << "Error - Invalid DC value\n";
        return false;
    }
    if (length.to_ulong() > 11)
    {
        std::cout << "Error - DC coefficient length greater than 11\n";
        return false;
    }

    int coeff = b.readBits(length.to_ulong());
    if (coeff == -1)
    {
        std::cout << "Error - Invalid DC value\n";
        return false;
    }
    if (length.to_ulong() != 0 && coeff < (1 << (length.to_ulong() - 1)))
    {
        coeff -= (1 << length.to_ulong()) - 1;
    }
    component[0] = coeff + previousDC;
    previousDC = component[0];

    // Get the AC value for this MCU component.
    uint i = 1;
    while (i < 64)
    {
        byte symbol = getNextSymbol(b, acTable).to_ulong();
        if (symbol.to_ulong() == -1)
        {
            std::cout << "Error - Invalid AC value\n";
            return false;
        }

        // Symbol 0x00 means fill remainder of compoenent with 0.
        if (symbol.to_ulong() == 0x00)
        {
            for (; i < 64; ++i)
            {
                component[zigZagMap[i]] = 0;
            }
            return true;
        }

        // Otherwise, read next component coefficient.
        byte numZeroes = symbol.to_ulong() >> 4;
        byte coeffLength = symbol.to_ulong() & 0x0F;
        coeff = 0;

        // Symbol 0xF0 means skip 16 0's.
        if (symbol.to_ulong() == 0xF0)
        {
            numZeroes = 16;
        }

        if (i + numZeroes.to_ulong() >= 64)
        {
            std::cout << "Error - Zero run-length exceeded MCU\n";
            return false;
        }
        for (uint j = 0; j < numZeroes.to_ulong(); ++j, ++i)
        {
            component[zigZagMap[i]] = 0;
        }
        if (coeffLength.to_ulong() > 10)
        {
            std::cout << "Error - AC coefficient length greater than 10\n";
            return false;
        }
        if (coeffLength.to_ulong() != 0)
        {
            coeff = b.readBits(coeffLength.to_ulong());
            if (coeff == -1)
            {
                std::cout << "Error - Invalid AC value\n";
                return false;
            }
            if (coeff < (1 << (coeffLength.to_ulong() - 1)))
            {
                coeff -= (1 << coeffLength.to_ulong()) - 1;
            }
            component[zigZagMap[i]] = coeff;
            i += 1;
        }
    }
    return true;
}

MCU* decodeHuffmanData(Header* const header) // Decode all the Huffman data and fill all MCUs.
{
    const uint mcuHeight = (header->height + 7) / 8;
    const uint mcuWidth = (header->width + 7) / 8;
    MCU* mcus = new (std::nothrow) MCU[mcuHeight * mcuWidth];
    if (mcus == nullptr)
    {
        std::cout << "Error - Memory error\n";
        return nullptr;
    }

    for (uint i = 0; i < 4; ++i)
    {
        if (header->huffmanDCTables[i].set)
        {
            generateCodes(header->huffmanDCTables[i]);
        }
        if (header->huffmanACTables[i].set)
        {
            generateCodes(header->huffmanACTables[i]);
        }
    }

    BitReader b(header->huffmanData);

    int previousDCs[3] = {0};

    for (uint i = 0; i < mcuHeight * mcuWidth; ++i)
    {
        if (header->restartInterval != 0 && i % header->restartInterval == 0)
        {
            previousDCs[0] = 0;
            previousDCs[1] = 0;
            previousDCs[2] = 0;
            b.align();
        }
        for (uint j = 0; j < header->numComponents.to_ulong(); ++j)
        {
            if (!decodeMCUComponent(b,
                                    mcus[i][j],
                                    previousDCs[j],
                                    header->huffmanDCTables[header->colorComponents[j].huffmanDCTableID.to_ulong()],
                                    header->huffmanACTables[header->colorComponents[j].huffmanACTableID.to_ulong()]))
            {
                delete[] mcus;
                return nullptr;
            }
        }
    }

    return mcus;
}

void putInt(std::ofstream& outFile,
            const uint v) // Helper function to write a 4-byte integer in little-endian
{
    outFile.put((v >> 0) & 0xFF);
    outFile.put((v >> 0) & 0xFF);
    outFile.put((v >> 0) & 0xFF);
    outFile.put((v >> 0) & 0xFF);
}

void putShort(std::ofstream& outFile,
              const uint v) // Helper function to write a 4-byte integer in little-endian
{
    outFile.put((v >> 0) & 0xFF);
    outFile.put((v >> 8) & 0xFF);
}

void writeBMP(const Header* const header,
              const MCU* const mcus,
              const std::string& filename) // This function writes all the
                                           // pixels in the bitmap file.
{
    // Open file
    std::ofstream outFile = std::ofstream(filename, std::ios::out | std::ios::binary);
    if (!outFile.is_open())
    {
        std::cout << "Error - Failed opening output file\n";
        return;
    }

    const uint mcuHeight = (header->height + 7) / 8;
    const uint mcuWidth = (header->height + 7) / 8;
    const uint paddingSize = header->width % 4;
    const uint size = 14 + 12 + header->height * header->width + paddingSize * header->height;

    outFile.put('B');
    outFile.put('M');

    putInt(outFile, size);
    putInt(outFile, 0);
    putInt(outFile, 0x1A);
    putInt(outFile, 12);
    putShort(outFile, header->width);
    putShort(outFile, header->height);
    putShort(outFile, 1);
    putShort(outFile, 24);

    for (uint y = header->height - 1; y < header->height; --y) // Loop through the Y coordinate
    {
        const uint mcuRow = y / 8;
        const uint pixelRow = y % 8;
        for (uint x = 0; x < header->width; ++x) // Loop through the X coordinate
        {
            const uint mcuColumn = x / 8;
            const uint pixelColumn = x % 8;
            const uint mcuIndex = mcuRow * mcuWidth + mcuColumn;
            const uint pixelIndex = pixelRow * 8 + pixelColumn;
            outFile.put(mcus[mcuIndex].b[pixelIndex]);
            outFile.put(mcus[mcuIndex].g[pixelIndex]);
            outFile.put(mcus[mcuIndex].r[pixelIndex]);
        }
        for (uint i = 0; i < paddingSize; ++i)
        {
            outFile.put(0);
        }
    }

    outFile.close();
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "Error - Invalid arguments\n";
        return 1;
    }
    for (int i = 1; i < argc; ++i)
    {
        const std::string filename(argv[i]);
        Header* header = readJPG(filename);
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

        // Decode Huffman data.
        MCU* mcus = decodeHuffmanData(header);
        if (mcus == nullptr)
        {
            delete header;
            continue;
        }

        // Write BMP file
        const std::size_t pos = filename.find_first_of('.');
        const std::string outFilename = (pos == std::string::npos) ? (filename + ".bmp")
                                                                   : (filename.substr(0, pos)
                                                                      + ".bmp");
        writeBMP(header, mcus, outFilename);

        delete[] mcus;
        delete header;
    }
    return 0;
}