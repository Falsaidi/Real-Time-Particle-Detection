#include <iostream>
#include <fstream>
#include <boost/asio.hpp>
#include <ctime>
#include <future>
#include <thread>
#include <random>
#include <vector>

#define pixelRemedianMatrix remedianMatrix[electrode][pixel]

using namespace boost::asio;
using boost::asio::ip::tcp;
using ip::tcp;
using std::string;
using std::cout;
using std::endl;

/*
*  ==========================================================
*  Variable Declarations
*  ==========================================================
*/

static bool writeFile;
static bool writeRemedians;
static bool writeSnippets;
static bool writeParticles;

/* <!> Network Data <!> */
static const int PORT = 12345;
//static const string IP = "169.254.239.247"; // Small laptop
//static const string IP = "169.254.196.209"; // Big laptop
static const string IP = "10.10.1.10";

static const int PACKET_LENGTH = 1036; // Length of packet in bytes

std::string dataPath = "data.bin";

/* <!> Snippet and Electrode Data <!> */
static const int WIDTH = 15;  // Radius around electrode centers
static const int SNIPPET_DEPTH = 30;  // # of frames to be saved at a time
static const int DEPTH = 10;  // Size of each pixel's remedian layer
static const int LAYERS = 3;  // # of remedian layers
static const int INTARR_SIZE = WIDTH * 2 + 1;  // Array size containing all pixels around electrode center
static const int THRESHOLD = 1000;  // Threshold to detect a particle
static int numElectrodes;
std::vector<int> electrodeLocations;
// 3-dimensional array to store snippet data over time for each electrode
std::vector<std::array<std::array<int16_t, INTARR_SIZE>, SNIPPET_DEPTH>> snippets;

std::vector<std::future<void>> pending_futures;  // Stores all output from async functions

std::vector<std::array<std::array<std::array<int16_t, DEPTH>, LAYERS>, INTARR_SIZE>> remedianMatrix;
std::vector<std::array<int16_t, INTARR_SIZE>> remedians;

std::vector<int16_t> electrodeWeights;

/*
*  ==========================================================
*  Inserting Function
*  ==========================================================
*  Inserts a value into the first spot of an array (removes the last element).
*/

void insertAtStart(std::array<int16_t, DEPTH>& arr, int16_t newVal) {
    // Shifts all values one spot to the right
    for (int i = DEPTH - 1; i > 0; i--) {
        arr[i] = arr[i - 1];
    }
    arr[0] = newVal;  // Inserts the new value at the beginning of the array
}

/*
*  ==========================================================
*  Averaging Function
*  ==========================================================
*  Calculates the average of an array.
*/

template <typename T, size_t SIZE>
T getAverage(std::array<T, SIZE> arr, int trueSize = SIZE) {
    // Sums all values in the array
    int sum = 0;
    for (int index = 0; index < trueSize; index++) {
        sum += arr[index];
    }
    T avg = sum / DEPTH;  // Gets average from the sum
    return avg;
}

template <size_t WIDTH, size_t HEIGHT>
int16_t average2D(std::array<std::array<int16_t, WIDTH>, HEIGHT> arr, int offset = 0) {
    // Calculates the average for each row in the array
    std::array<int16_t, HEIGHT> averages;
    std::array<int16_t, WIDTH> currentRow;
    int arrSize = HEIGHT - offset * 2;
    for (int row = offset; row < HEIGHT - offset; row++) {
        for (int column = 0; column < WIDTH; column++) {
            currentRow[column] = arr[row][column];
        }
        averages[row] = getAverage<int16_t, WIDTH>(currentRow);
    }

    return getAverage<int16_t, HEIGHT>(averages, arrSize);  // Total average of the array
}

/*
*  ==========================================================
*  Two's Complement Function
*  ==========================================================
*  Takes the two's complement of the concatenation of two 8-bit numbers.
*/
int16_t twos(int msb, int lsb) {
    int16_t complement = msb << 8 | lsb; // Combines all four bytes into a 16-bit number
    return complement;
}

/*
*  ==========================================================
*  Particle Write Function
*  ==========================================================
*  Writes a detected particle into a file.
*  Useful for graphing particles as they're received.
*/

void writeParticlesCSV(int chosenElectrode, int frame, int weight, int similar) {
    std::ofstream file("particles.csv", std::ios::out | std::ios::app);
    file << chosenElectrode << "," << frame << "," << weight << "," << similar << "\n";
    for (std::array<std::array<int16_t, INTARR_SIZE>, SNIPPET_DEPTH> snippet : snippets) {
        for (std::array<int16_t, INTARR_SIZE> column : snippet) {
            for (int16_t pixel : column) {
                file << pixel << ",";
            }
        }
        file << "\n";
    }
}

/*
*  ==========================================================
*  CSV Snippet Write Function
*  ==========================================================
*  Writes a snippet to its associated .csv file.
*  Useful for graphing each snippet individually.
*/
void writeSnippetCSV(std::array<int16_t, INTARR_SIZE> data, int frame, int segment, int sensor, int led_config, int electrode) {
    // Opens the corresponding snippet output file in append mode
    std::ofstream file("out" + std::to_string(electrode) + ".csv", std::ios::out | std::ios::app);
    file << frame << "," << segment << "," << sensor << "," << led_config;  // Writes the initial packet information
    // Writes all pixel values minus their respective remedians
    for (int pixel = 0; pixel < INTARR_SIZE; pixel++) {
        file << "," << data[pixel] - remedians[electrode][pixel];
    }
    for (int pixel = 0; pixel < INTARR_SIZE; pixel++) {
        file << "," << data[pixel];
    }
    file << "\n";  // Writes a newline character to separate packets
    file.close();
}

/*
*  ==========================================================
*  CSV Remedian Write Function
*  ==========================================================
*  Writes all pixel remedians for a snippet into its corresponding .csv file.
*  Useful for tracking remedian values in case of error.
*/

void writeRemediansCSV(int electrode) {
    std::ofstream file("remedians" + std::to_string(electrode) + ".csv", std::ios::out | std::ios::app);  // Opens the remedian output file in append mode
    // Loops through all remedians for the snippet and writes them to the file
    for (int16_t remedian : remedians[electrode]) {
        file << remedian << ",";
    }
    file << "\n";
    file.close();
}

/*
*  ==========================================================
*  Location Write Function
*  ==========================================================
*  Writes all particle locations into a .csv file.
*  Useful for verifying particle locations using post-processed data.
*/

void writeLocation(int frame, int electrode) {
    std::ofstream file("locations.csv", std::ios::out | std::ios::app);
    file << frame << "," << electrodeLocations[electrode] << "," << electrodeWeights[electrode] << "\n";
    file.close();
}

/*
*  ==========================================================
*  Segment Write Function
*  ==========================================================
*  Writes all segments into a .csv file.
*  Useful for plotting the full raw data.
*/

void writeSegment(int frame, int segment, std::array<uint8_t, PACKET_LENGTH> packet) {
    std::ofstream file("fullData.csv", std::ios::out | std::ios::app);
    file << frame << "," << segment << "\n";
    for (int i = 0; i < PACKET_LENGTH; i += 2) {
        file << twos(packet[i + 1], packet[i]) << ",";
    }
    file << "\n";
    file.close();
}

/*
*  ==========================================================
*  Binary Write Function
*  ==========================================================
*  Writes all binary data to a file given by the file path when calling the program (default "data.bin")
*/

void writeToFile(std::array<uint8_t, PACKET_LENGTH> packet) {
    std::ofstream file(dataPath, std::ios::out | std::ios::binary | std::ios::app);  // Opens the file in append mode
    // Loops through each byte in the packet and writes it to the file
    for (int i = 0; i < packet.size(); i++) {
        file.write((char*)&packet[i], 1);
    }
    file.close();
}

/*
*  ==========================================================
*  Remedian Calculation Function
*  ==========================================================
*  Calculates the remedian for a pixel of a snippet.
*/

void calculateRemedian(int electrode, int pixel, int16_t val) {
    insertAtStart(pixelRemedianMatrix[0], val);  // Inserts the new pixel value in the first array in the remedian matrix

    // If the first array is full, gets the average of that array and inserts that into the next array
    if (pixelRemedianMatrix[0][DEPTH - 1] != 0) {
        int16_t prevLayerAvg = getAverage<int16_t, DEPTH>(pixelRemedianMatrix[0]);

        // Inserts the previous layer's average into the next layer, repeating while each successive array is full
        bool loop = true;
        int layer = 1;
        while (loop && layer < LAYERS - 1) {
            insertAtStart(pixelRemedianMatrix[layer], prevLayerAvg);  // Inserts the previous array average into the current layer

            // If the current layer isn't full, break the loop
            if (pixelRemedianMatrix[layer][DEPTH - 1] == 0) {
                loop = false;
            }
            // Otherwise get the average of the current layer and move to the next layer
            else {
                prevLayerAvg = getAverage<int16_t, DEPTH>(pixelRemedianMatrix[layer]);
                layer++;
            }
        }

        // Inserts the second-last layer's average into the deepest layer (if it was full)
        if (layer == LAYERS - 1) {
            insertAtStart(pixelRemedianMatrix[LAYERS - 1], prevLayerAvg);
        }

        // Clears all full layers
        int index = 0;
        while (index < layer - 1) {
            for (int i = 0; i < DEPTH; i++) {
                pixelRemedianMatrix[index][i] = 0;
            }
            index++;
        }
    }

    // Sets the new remedian for that pixel to the average of the deepest layer
    remedians[electrode][pixel] = getAverage<int16_t, DEPTH>(pixelRemedianMatrix[LAYERS - 1]);
}

/*
*  ==========================================================
*  Particle Weight Calculation Function
*  ==========================================================
*  Calculates the 2D average of a snippet image and determines if a particle is detected.
*/

void detectParticle(std::array<int16_t, INTARR_SIZE> snippet, int frame, int segment, int electrode, clock_t clock) {
    // Subtracts the remedian value for each pixel in the snippet
    std::array<int16_t, INTARR_SIZE> subbedSnippet;
    for (int pixel = 0; pixel < INTARR_SIZE; pixel++) {
        subbedSnippet[pixel] = abs(snippet[pixel] - remedians[electrode][pixel]);
    }

    // Puts the snippet at the front of its associated array, erasing the oldest snippet
    for (int i = SNIPPET_DEPTH - 1; i > 0; i--) {
        snippets[electrode][i] = snippets[electrode][i - 1];
    }
    snippets[electrode][0] = subbedSnippet;

    // Takes a 2D average of the backgruond subtracted snippet matrix
    int16_t electrodeWeight = average2D<INTARR_SIZE, SNIPPET_DEPTH>(snippets[electrode]);
    electrodeWeights[electrode] = electrodeWeight;

    // If all electrodes had their average calculated
    if (electrode == numElectrodes - 1) {

        // Determines which averages are above the threshold
        std::vector<int> aboveThreshold;
        for (int index = 0; index < numElectrodes; index++) {
            if (abs(electrodeWeights[index]) >= THRESHOLD) {
                aboveThreshold.push_back(index);
            }
        }

        // If there is an average above the threshold
        if (aboveThreshold.size() >= 1) {
            // If only one average is above the threshold
            if (aboveThreshold.size() == 1) {
                if(writeParticles) writeParticlesCSV(aboveThreshold[0], frame, electrodeWeights[aboveThreshold[0]], 0);
            }
            // If more than one average is above the threshold
            else if (aboveThreshold.size() > 1) {

                // While there is still more than one average above the threshold
                int offset = 1;
                while (aboveThreshold.size() > 1 && offset < WIDTH - 1) {
                    // Take the 2D average with a smaller window from the snippet matrix
                    std::vector<int> newAboveThreshold;
                    for (int electrode : aboveThreshold) {
                        int16_t centeredWeight = average2D<INTARR_SIZE, SNIPPET_DEPTH>(snippets[electrode], offset);
                        // Save the electrode if its average is still above the threshold
                        if (abs(centeredWeight) >= THRESHOLD) {
                            newAboveThreshold.push_back(electrode);
                        }
                    }
                    aboveThreshold = newAboveThreshold;
                    offset++;
                }
                // If there is still more than one average
                if (aboveThreshold.size() > 1 || aboveThreshold.size() == 0) {
                    int16_t maxWeight = 0;
                    int maxWeightIndex;
                    // Finds the electrode with the highest average
                    for (int index = 0; index < numElectrodes; index++) {
                        if (abs(electrodeWeights[index]) > abs(maxWeight)) {
                            maxWeight = electrodeWeights[index];
                            maxWeightIndex = index;
                        }
                    }
                    aboveThreshold = { maxWeightIndex };
                }
                if(writeParticles) writeParticlesCSV(aboveThreshold[0], frame, electrodeWeights[aboveThreshold[0]], 1);
                
            }
            writeLocation(frame, aboveThreshold[0]);  // Saves the particle location

            // Sends the electrode that contains the particle to the Arduino
            boost::asio::io_context io_service;
            serial_port port(io_service);
            port.open("COM6");
            unsigned char szBuff[1] = { aboveThreshold[0] };
            port.write_some(buffer(szBuff, 1));
        }
    }
    double duration = (std::clock() - clock) / (double)CLOCKS_PER_SEC;
    cout << "Runtime: " << duration * 1000 << "ms" << endl;
}

/*
*  ==========================================================
*  Remedian Function
*  ==========================================================
*  Calls the remedian calculation function for each pixel in the snippet.
*/

void findRemedian(std::array<int16_t, INTARR_SIZE> snippet, int electrode) {
    // Finds the remedian for each pixel in the snippet
    for (int pixel = 0; pixel < INTARR_SIZE; pixel++) {
        calculateRemedian(electrode, pixel, snippet[pixel]);
    }

    if (writeRemedians) {
        writeRemediansCSV(electrode);
    }
}

/*
*  ==========================================================
*  Ethernet Read Function
*  ==========================================================
*  Reads data over ethernet, given a packet length.
*  The data is stored into a uint8_t array, the standard type for unsigned byte data.
*/

std::array<uint8_t, PACKET_LENGTH> read_(tcp::socket& socket) {
    std::array<uint8_t, PACKET_LENGTH> buf;  // uint8_t buffer to store the data
    boost::asio::read(socket, boost::asio::buffer(buf), boost::asio::transfer_exactly(PACKET_LENGTH));  // Reads a packet of a certain length to the buffer
    return buf;  // Returns the buffer
}

/*
*  ==========================================================
*  Snippet Processing Function
*  ==========================================================
*  Extracts all snippets from the packet and processes them.
*  Processing includes calculating remedians for each pixel and detecting a particle.
*/

void processSnippets(std::array<uint8_t, PACKET_LENGTH> packet, int frame, int segment, int sensor, int led_config, clock_t clock) {
    /* <!> Snippet Data Extraction and Processing <!> */
    // Saves one small snippet from the data, given a position and width

    std::array<int16_t, INTARR_SIZE> snippet;
    int location;
    int byteIndex;

    // Loops through each electrode position, checking to see if the snippet contains any electrodes
    for (int electrode = 0; electrode < numElectrodes; electrode++) {
        location = electrodeLocations[electrode] * 2; // Multiplies the location by 2 because the data is made of 2 bytes per pixel
        location -= (segment - 1) * 1024;  // Normalizes the location relative to the current segment

        // If the location is within the range 0-1023, the segment contains that electrode
        if (0 <= location && location <= 1023) {
            // Saves the snippet to the snippet array
            for (int index = 0; index <= WIDTH * 2; index++) {
                byteIndex = location - (WIDTH * 2) + (index * 2);  // Calculates the current index of the snippet in the raw packet
                snippet[index] = twos(packet[byteIndex + 1], packet[byteIndex]);  // Stores the twos complement of the pixel bytes in the snippet array
            }

            // Asynchronously calls functions to calculate the remedian of the snippet and process it
            auto calcRemedian = std::async(std::launch::async, findRemedian, snippet, electrode);
            pending_futures.push_back(std::move(calcRemedian));

            auto process = std::async(std::launch::async, detectParticle, snippet, frame, segment, electrode, clock);
            pending_futures.push_back(std::move(process));

            // Writes snippet data if needed
            if (writeSnippets) {
                writeSnippetCSV(snippet, frame, segment, sensor, led_config, electrode);
            }
        }
    }
}

/*
*  ==========================================================
*  Main Function
*  ==========================================================
*/

int main(int argc, char* argv[]) {

    std::array<uint8_t, PACKET_LENGTH> packet;
    boost::asio::io_context io_service; // I/O context needed for reading the ethernet data

    /* <!> Input Parameters Processing <!> */
    // Reads all data inputted to the program.
    if (argc > 1) {  // If arguments are given to the program
        dataPath = argv[1];
        writeFile = std::stoi(argv[2]);
        writeSnippets = std::stoi(argv[3]);
        writeRemedians = std::stoi(argv[4]);
        writeParticles = std::stoi(argv[5]);
        for (int index = 5; index < argc; index++) {
            electrodeLocations.push_back(std::stoi(argv[index]));
        }
        numElectrodes = argc - 5;
    }
    else {  // If no arguments were given, use default data
        dataPath = "data.bin";
        writeFile = false;
        writeSnippets = false;
        writeRemedians = false;
        writeParticles = false;
        electrodeLocations = { 500, 750, 1000, 1250 };
        numElectrodes = 4;
    }

    // Expands all vectors to the needed size
    for (int dynamicSize = 0; dynamicSize < numElectrodes; dynamicSize++) {
        std::array<std::array<int16_t, INTARR_SIZE>, SNIPPET_DEPTH> emptySnippet = { {} };
        std::array<std::array<std::array<int16_t, DEPTH>, LAYERS>, INTARR_SIZE> emptyMatrix = { { {} } };
        std::array<int16_t, INTARR_SIZE> emptyRemedian = {};

        snippets.push_back(emptySnippet);
        remedianMatrix.push_back(emptyMatrix);
        remedians.push_back(emptyRemedian);
        electrodeWeights.push_back(0);
    }

    cout << "C++ Program Start" << endl;

    /* <!> Ethernet Connection <!> */
    //listen for new connection
    boost::system::error_code ec;
    tcp::acceptor acceptor_(io_service, tcp::endpoint(boost::asio::ip::address::from_string(IP), PORT));
    tcp::socket socket_(io_service);  // Creates a socket for the connection
    acceptor_.accept(socket_);  // Accepts the connection
    cout << "accepted" << endl;

    int count = 0;
    int iter = 453320;
    // Continuously listens for new data
    while (true) {

        // Starts a clock to time the program
        std::clock_t start;
        start = std::clock();

        // Reads the packet data when a packet is received
        packet = read_(socket_);
        //cout << "message received" << endl;

        // Stores the initial packet information
        int frame = packet[0] << 24 | packet[1] << 16 | packet[2] << 8 | packet[3];
        int segment = packet[4];
        int sensor = packet[5];
        int led_config = packet[6] << 8 | packet[7];

        /* <!> Data Collection and Storage <!> */
        // Appends the binary data to the data file (given by filePath)
        if (writeFile) {
            writeToFile(packet);
        }

        processSnippets(packet, frame, segment, sensor, led_config, start);

        //writeSegment(frame, segment, packet);

        // Stops the clock and prints the runtime
        count += 1;
    }

    return 0;
}