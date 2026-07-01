#include "registers.hpp"

// Setters
void Registers::setR0(uint32_t value) {
    R0 = value;
}

void Registers::setR1(uint32_t value) {
    R1 = value;
}

void Registers::setR2(uint32_t value) {
    R2 = value;
}

void Registers::setR3(uint32_t value) {
    R3 = value;
}

void Registers::setR4(uint32_t value) {
    R4 = value;
}

void Registers::setR5(uint32_t value) {
    R5 = value;
}

void Registers::setR6(uint32_t value) {
    R6 = value;
}

void Registers::setR7(uint32_t value) {
    R7 = value;
}

void Registers::setR8(uint32_t value) {
    R8 = value;
}

void Registers::setR9(uint32_t value) {
    R9 = value;
}

void Registers::setR10(uint32_t value) {
    R10 = value;
}

void Registers::setR11(uint32_t value) {
    R11 = value;
}

void Registers::setR12(uint32_t value) {
    R12 = value;
}

void Registers::setSP_R13(uint32_t value) {
    SP_R13 = value;
}

void Registers::setLR(uint32_t value) {
    LR = value;
}

void Registers::setPC(uint32_t value) {
    PC = value;
}

void Registers::setPSR(uint32_t value) {
    PSR = value;
}

void Registers::setPRIMASK(uint32_t value) {
    PRIMASK = value;
}

void Registers::setFAULTMASK(uint32_t value) {
    FAULTMASK = value;
}

void Registers::setBASEPRI(uint32_t value) {
    BASEPRI = value;
}

void Registers::setCONTROL(uint32_t value) {
    CONTROL = value;
}


// Getters
uint32_t Registers::getR0() const {
    return R0;
}

uint32_t Registers::getR1() const {
    return R1;
}

uint32_t Registers::getR2() const {
    return R2;
}

uint32_t Registers::getR3() const {
    return R3;
}

uint32_t Registers::getR4() const {
    return R4;
}

uint32_t Registers::getR5() const {
    return R5;
}

uint32_t Registers::getR6() const {
    return R6;
}

uint32_t Registers::getR7() const {
    return R7;
}

uint32_t Registers::getR8() const {
    return R8;
}

uint32_t Registers::getR9() const {
    return R9;
}

uint32_t Registers::getR10() const {
    return R10;
}

uint32_t Registers::getR11() const {
    return R11;
}

uint32_t Registers::getR12() const {
    return R12;
}

uint32_t Registers::getSP_R13() const {
    return SP_R13;
}

uint32_t Registers::getLR() const {
    return LR;
}

uint32_t Registers::getPC() const {
    return PC;
}

uint32_t Registers::getPSR() const {
    return PSR;
}

uint32_t Registers::getPRIMASK() const {
    return PRIMASK;
}

uint32_t Registers::getFAULTMASK() const {
    return FAULTMASK;
}

uint32_t Registers::getBASEPRI() const {
    return BASEPRI;
}

uint32_t Registers::getCONTROL() const {
    return CONTROL;
}
