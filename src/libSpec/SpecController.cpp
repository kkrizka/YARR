// #################################
// # Author: Timon Heim
// # Email: timon.heim at cern.ch
// # Project: Yarr
// # Description: SPEC cpp library
// # Comment: Original driver taken from Marcus Guillermo
// #          Modified for SPEC card
// ################################

#include <stdlib.h>
#include <iostream>
#include <string.h>

#include <SpecController.h>
#include <GennumRegMap.h>

#define DEBUG

SpecController::SpecController(unsigned int id) {
    specId = id;
    try {
        this->init();
        this->configure();
    } catch (Exception &e) {
        std::cerr << __PRETTY_FUNCTION__ << " -> " << e.toString() << std::endl;
        std::cerr << __PRETTY_FUNCTION__ <<  " -> Fatal Error! Aborting!"  << std::endl;
        exit(-1);
    }
}

SpecController::~SpecController() {
    spec->unmapBAR(0, bar0);
    spec->unmapBAR(4, bar4);
    spec->close();
    delete spec;
}

void SpecController::writeSingle(uint32_t off, uint32_t val) {
    this->write32(bar0, off, val);
}

uint32_t SpecController::readSingle(uint32_t off) {
    return this->read32(bar0, off);
}

void SpecController::writeBlock(uint32_t off, uint32_t *val, size_t words) {
    this->writeBlock(bar0, off, val, words);
}

void SpecController::readBlock(uint32_t off, uint32_t *val, size_t words) {
    this->readBlock(bar0, off, val, words);
}

void SpecController::writeDma(uint32_t off, uint32_t *data, size_t words) {
    int status = this->getDmaStatus(); 
    if ( status == DMAIDLE || status == DMADONE || status == DMAABORTED ) {
        UserMemory *um = &spec->mapUserMemory(data, words*4, false);
        KernelMemory *km = &spec->allocKernelMemory(sizeof(struct dma_linked_list)*um->getSGcount());

        struct dma_linked_list *llist = this->prepDmaList(um, km, off, 1);
        
        uint32_t *addr = (uint32_t*) bar0+DMACSTARTR;
        memcpy(addr, &llist[0], sizeof(struct dma_linked_list));
        this->startDma();

        spec->waitForInterrupt(0);
        
        // Ackowledge interrupt
        volatile uint32_t irq_ack = this->read32(bar4, GNGPIO_INT_STATUS/4);
        (void) irq_ack;

        delete km;
        delete um;
    } else {
        std::cerr << __PRETTY_FUNCTION__ << " -> " 
            << "DMA Transfer aborted (Status = 0x" << std::hex << status << std::dec << ")" << std::endl;
    }
}

void SpecController::readDma(uint32_t off, uint32_t *data, size_t words) {
    int status = this->getDmaStatus(); 
    if ( status == DMAIDLE || status == DMADONE || status == DMAABORTED ) {
        UserMemory *um = &spec->mapUserMemory(data, words*4, false);
        KernelMemory *km = &spec->allocKernelMemory(sizeof(struct dma_linked_list)*um->getSGcount());

        struct dma_linked_list *llist = this->prepDmaList(um, km, off, 0);
        
        uint32_t *addr = (uint32_t*) bar0+DMACSTARTR;
        memcpy(addr, &llist[0], sizeof(struct dma_linked_list));
        this->startDma();

        spec->waitForInterrupt(0);
        
        // Ackowledge interrupt
        volatile uint32_t irq_ack = this->read32(bar4, GNGPIO_INT_STATUS/4);
        (void) irq_ack;

        um->sync(UserMemory::BIDIRECTIONAL);

        delete km;
        delete um;
    } else {
        std::cerr << __PRETTY_FUNCTION__ << " -> " 
            << "DMA Transfer aborted (Status = 0x" << std::hex << status << std::dec << ")" << std::endl;
    }
}

void SpecController::init() {
#ifdef DEBUG
    std::cout << __PRETTY_FUNCTION__ << "-> Opening SPEC with id #" << specId << std::endl;
#endif
    // Init SPEC
    try {
        spec = new SpecDevice(specId);
    } catch (Exception &e) {
        std::cerr << __PRETTY_FUNCTION__ << " -> " << e.toString() << std::endl;
        throw Exception(Exception::INIT_FAILED);
        return;
    }
    // Open SPEC
    spec->open();
#ifdef DEBUG
    std::cout << __PRETTY_FUNCTION__ << " -> Mapping BARs" << std::endl;
#endif
    // Map BARs
    try {
        bar0 = spec->mapBAR(0);
#ifdef DEBUG
        std::cout << __PRETTY_FUNCTION__ << " -> Mapped BAR0 at 0x" << std::hex << bar0 
            << " with size 0x" << spec->getBARsize(0) << std::dec << std::endl;
#endif
        bar4 = spec->mapBAR(4);
#ifdef DEBUG
        std::cout << __PRETTY_FUNCTION__ << " -> Mapped BAR4 at 0x" << std::hex << bar4 
            << " with size 0x" << spec->getBARsize(4) << std::dec << std::endl;
#endif
    } catch (Exception &e) {
        std::cerr << __PRETTY_FUNCTION__ << " -> " << e.toString() << std::endl;
        throw Exception(Exception::INIT_FAILED);
        return;
    }
    return;
}

void SpecController::configure() {
#ifdef DEBUG
        std::cout << __PRETTY_FUNCTION__ << "-> Configuring GN412X" << std::endl;
#endif
     
    // Activate MSI if necessary
    if (read32(bar4, GNPPCI_MSI_CONTROL/4) != 0x00A55805) {
#ifdef DEBUG
        std::cout << __PRETTY_FUNCTION__ << "-> MSI needs to be configured!" << std::endl;
#endif
        this->write32(bar4,GNPPCI_MSI_CONTROL/4, 0x00A55805);
    }
 
    // Reset INTx vectors
    for (int i=0; i<8; i++) this->write32(bar4, GNINT_CFG(i)/4, 0x0);

    // Configure INTx vector given by MSI_DATA&0x3
    this->write32(bar4,GNINT_CFG(this->read32(bar4, GNPPCI_MSI_DATA/4)&0x3)/4, 0x800c); 

    // We are using GPIO8/9 as interrupt, make sure they are not in bypass mode
    this->write32(bar4,GNGPIO_BYPASS_MODE/4, 0x0000); 

    // Set intterupt GPIO 8 and 9 to be in input mode = 1
    this->write32(bar4,GNGPIO_DIRECTION_MODE/4, 0xFFFF);
    
    // Disable output
    this->write32(bar4,GNGPIO_OUTPUT_ENABLE/4, 0x0000);
    
    // Edge trigger mode = 0
	this->write32(bar4,GNGPIO_INT_TYPE/4, 0x0);

    // Trigger on high value = 1
	this->write32(bar4,GNGPIO_INT_VALUE/4, 0x300);

    // Trigger on edge specified in GNGPIO_INT_TYPE
	this->write32(bar4,GNGPIO_INT_ON_ANY/4, 0x0);
    
    // Enable our GPIOs as an interrupt source and disable all others
	this->write32(bar4,GNGPIO_INT_MASK_SET/4, 0xFFFF);
    this->write32(bar4,GNGPIO_INT_MASK_CLR/4, 0x0300);

    // Clear All IRQs
    this->write32(bar4,GNINT_STAT/4, 0xFFF0);
    this->write32(bar4,GNINT_STAT/4, 0x0000);
    volatile uint32_t res1 = this->read32(bar4,GNINT_STAT/4);
    (void) res1;

    // Reset GPIO INT STATUS
    volatile uint32_t res2 = this->read32(bar4,GNGPIO_INT_STATUS/4);
    (void) res2;

    usleep(200);

    // Clear IRQ queues
    spec->clearInterruptQueue(0);
    spec->clearInterruptQueue(1);
}

void SpecController::write32(void *bar, uint32_t off, uint32_t val) {
    uint32_t *addr = (uint32_t*) bar+off;
    *addr = val;
}

uint32_t SpecController::read32(void *bar, uint32_t off) {
    uint32_t *addr = (uint32_t*) bar+off;
    return *addr;
}

void SpecController::mask32(void *bar, uint32_t off, uint32_t mask, uint32_t val) {
    uint32_t *addr = (uint32_t*) bar+off;
    uint32_t tmp = *addr;
    tmp &= ~mask;
    tmp |= val;
    *addr = tmp;
}

void SpecController::writeBlock(void *bar, uint32_t off, uint32_t *val, size_t words) {
    uint32_t *addr = (uint32_t*) bar+off;
    memcpy(addr, val, words*4);
}

void SpecController::readBlock(void *bar, uint32_t off, uint32_t *val, size_t words) {
    uint32_t *addr = (uint32_t*) bar+off;
    for(unsigned int i=0; i<words; i++) val[i] = *addr++;
}


struct dma_linked_list* SpecController::prepDmaList(UserMemory *um, KernelMemory *km, uint32_t off, bool write) {
    struct dma_linked_list *llist = (struct dma_linked_list*) km->getBuffer();
    uint32_t dev_off = off*4;
    unsigned int j = 0;
    for (unsigned int i=0; i<um->getSGcount(); i++) {
        int sg_size = um->getSGentrySize(i);
        uint32_t sg_addr_h = ((uint64_t)um->getSGentryAddress(i) >> 32);
        uint32_t sg_addr_l = ((um->getSGentryAddress(i)) & 0xFFFFFFFF);
        do {
            uint32_t fixed_size = sg_size;
            if (sg_size > 4096) fixed_size = 4096;
            llist[j].carrier_start = dev_off;
            //llist[j].carrier_start = 0;
            dev_off += fixed_size;
            llist[j].host_start_l = sg_addr_l;
            llist[j].host_start_h = sg_addr_h;
            llist[j].length = fixed_size;
            uint64_t next = km->getPhysicalAddress();
            next += (sizeof(struct dma_linked_list) * ( j + 1 ));
            llist[j].host_next_l = (uint32_t)((uint64_t)next & 0xFFFFFFFF);
            llist[j].host_next_h = (uint32_t)((uint64_t)next >> 32);
            llist[j].attr = 0x1 + (write << 1); // L2P, not last
#if 0
            std::cout << "Linked List Entry [" << std::dec << j << std::hex << "]:" << std::endl;
            std::cout << "  Carrier Start: 0x" << llist[j].carrier_start << std::endl;
            std::cout << "  Host Start H:  0x" << llist[j].host_start_h << std::endl;
            std::cout << "  Host Start L:  0x" << llist[j].host_start_l << std::endl;
            std::cout << "  Length:        " << std::dec << llist[j].length << std::hex << std::endl;
            std::cout << "  Host Next L    0x" << llist[j].host_next_l << std::endl;
            std::cout << "  Host Next H    0x" << llist[j].host_next_h << std::endl;
            std::cout << "  Attribute      0x" << llist[j].attr << std::endl;
#endif
            sg_size = sg_size - 4096;
            sg_addr_l = sg_addr_l + 4096; // FIXME: Can this overflow ?
            j++;
        } while (sg_size > 0);
    }
    // Mark last item
    llist[j-1].host_next_l = 0x0;
    llist[j-1].host_next_h = 0x0;
    llist[j-1].attr = 0x0 + (write << 1); // last item
#if 0
    std::cout << "Modified Last Item[" << std::dec << j-1 << "]" << std::hex << std::endl;
    std::cout << "  Host Next L    0x" << llist[j-1].host_next_l << std::endl;
    std::cout << "  Host Next H    0x" << llist[j-1].host_next_h << std::endl;
    std::cout << "  Attribute      0x" << llist[j-1].attr << std::endl;// L2P, last item
    std::cout << std::dec;
#endif

    // Sync Memory
    km->sync(KernelMemory::BIDIRECTIONAL);
    return llist;
}

void SpecController::startDma() {
    uint32_t *addr = (uint32_t*) bar0+DMACTRLR;
    // Set t 0x1 to start DMA transfer
    *addr = 0x1;
}

uint32_t SpecController::getDmaStatus() {
    uint32_t *addr = (uint32_t*) bar0+DMASTATR;
    return *addr;
}

void program(const void *data, size_t size) {
    // Stuff perhaps missing, but in manual
    // FCL_IM -> enable the right 
    
    // Setup BOOT_SEL signals in GPIO 14,15 to 0, 1
    
    // FCL_CLK_DIV -> 0x0 -> PCLK/2 (PCLK = 125MHz)
    
    // FCL_CTRL -> 0x40 -> Reset
    // Check reset is high
    // FCL_CTRL -> 0x0
    
    // FCL_IRQ -> 0x0 -> Clear pending IRQ
    
    // Setup FCL CTRL
    // 0x2 - SPRI_EN
    // 0x4 - FSM_EN
    // 0x30 - Last Byte CNT -> (size & 0x3)
    // 0x100 - SPRI_CLK_STOP_EN

    // FCL_TIMER_CTRL -> 0x0
    // FCL_TIMER_0 -> 0x10
    // FCL_TIMER_1 -> 0x0
    
    // Enable the right lines
    // FCL_EN -> 0x17
    
    // Start FSM
    // FCL_CTRL += 0x1

    // Write a bit of data to FCL_FIFO
    // Wait until FCL_IRQ & 0x5 = 1
    // Loop
    // FCL_CTRL -> 0x186 (last data written)
}
