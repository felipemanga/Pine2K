void init(bool crashed, u32 crashLocation);
void update();

const auto magic = 0xFEEDBEEF;
const auto magicPtr = reinterpret_cast<volatile u32*>(0x20004000);
const auto reset = 0x05FA0004;
const auto resetPtr = reinterpret_cast<volatile u32*>(0xE000ED0C);
bool devmode = ((volatile uint32_t *) 0xE000ED00)[0] == 1947;

#define ASM(x...) __asm__ volatile (".syntax unified\n" #x)
void write_command_16(uint16_t data);
void write_data_16(uint16_t data);

void __attribute__((naked)) flush(pine::u16 *data, u32 len){
    ASM(
        push {r4, r5, lr}       \n
        ldr r2, =0xA0002188     \n
        ldr r3, =1<<12          \n
        movs r4, 252            \n
        1:                      \n
        ldrh r5, [r0];          \n
        lsls r5, 3              \n
        str r5, [r2];           \n
        str r3, [r2, r4];       \n
        adds r0, 2              \n
        subs r1, 1              \n
        str r3, [r2, 124];      \n
        bne 1b                  \n
        pop {r4, r5, pc}        \n
        );
}

void setWindow(int x, int w, int y, int h){
    write_command(0x38); write_data(x + w - 1);
    write_command(0x39); write_data(x);
    write_command(0x36); write_data(y + h - 1);
    write_command(0x37); write_data(y);
    write_command(0x20); write_data(y);
    write_command(0x21); write_data(x);
}


/* */
extern "C" {
    void __attribute__((naked)) HardFault_Handler(void) {

        ASM(
            ldr r0, =0xE000ED00 \n
            ldr r0, [r0]        \n
            ldr r1, =1947       \n
            cmp r0, r1          \n
            bne 1f              \n
            ldr r0, [sp, 0x14]  \n
            ldr r1, =0x20000000 \n
            cmp r0, r1          \n
            bge 1f              \n
            ldr r0, [sp, 0x18]  \n
            ldr r1, =0x20000000 \n
            cmp r0, r1          \n
            bge 1f              \n
            bx lr               \n
            1:                  \n
            ldr r0, =0x20004000 \n
            ldr r1, =0xFEEDBEEF \n
            ldr r2, [sp, 0x18]  \n
            ldr r3, [sp, 0x14]  \n
            stm r0!, {r1-r3}    \n
            ldr r6, =projectName\n
            ldm r6!, {r1-r4}    \n
            stm r0!, {r1-r5}    \n
            ldr r1, =0x05FA0004 \n
            ldr r0, =0xE000ED0C \n
            str r1, [r0]        \n
            );
    }
}
/* */

int main(){
    {
        bool crashed = *magicPtr == magic;
        u32 crashLocation = magicPtr[1] >= 0x20000000 && magicPtr[1] < 0x20000800 ? magicPtr[1] : magicPtr[2];
        if( !crashed && !devmode ){
            PC::begin();
        } else {
            PC::init();
            PD::begin();
        }

        if(crashed && crashLocation < 0x20000000 ) crashed = false;
        init(crashed, crashLocation);
    }
    while(true){
        if(PC::update())
            update();
    }
}
