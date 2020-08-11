set(
        riscv_insn_list
        add
        addi
        addiw
        addw
        amoadd_d
        amoadd_w
        amoand_d
        amoand_w
        amomax_d
        amomaxu_d
        amomaxu_w
        amomax_w
        amomin_d
        amominu_d
        amominu_w
        amomin_w
        amoor_d
        amoor_w
        amoswap_d
        amoswap_w
        amoxor_d
        amoxor_w
        and
        andi
        auipc
        beq
        bge
        bgeu
        blt
        bltu
        bne
        csrrc
        csrrci
        csrrs
        csrrsi
        csrrw
        csrrwi
        div
        divu
        divuw
        divw
        fadd_d
        fadd_s
        fclass_d
        fclass_s
        fcvt_d_l
        fcvt_d_lu
        fcvt_d_s
        fcvt_d_w
        fcvt_d_wu
        fcvt_l_d
        fcvt_l_s
        fcvt_lu_d
        fcvt_lu_s
        fcvt_s_d
        fcvt_s_l
        fcvt_s_lu
        fcvt_s_w
        fcvt_s_wu
        fcvt_w_d
        fcvt_w_s
        fcvt_wu_d
        fcvt_wu_s
        fdiv_d
        fdiv_s
        fence
        fence_i
        feq_d
        feq_s
        fld
        fle_d
        fle_s
        flt_d
        flt_s
        flw
        fmadd_d
        fmadd_s
        fmax_d
        fmax_s
        fmin_d
        fmin_s
        fmsub_d
        fmsub_s
        fmul_d
        fmul_s
        fmv_d_x
        fmv_s_x
        fmv_x_d
        fmv_x_s
        fnmadd_d
        fnmadd_s
        fnmsub_d
        fnmsub_s
        fsd
        fsgnj_d
        fsgnjn_d
        fsgnjn_s
        fsgnj_s
        fsgnjx_d
        fsgnjx_s
        fsqrt_d
        fsqrt_s
        fsub_d
        fsub_s
        fsw
        jal
        jalr
        lb
        lbu
        ld
        lh
        lhu
        lr_d
        lr_w
        lui
        lw
        lwu
        mul
        mulh
        mulhsu
        mulhu
        mulw
        or
        ori
        rem
        remu
        remuw
        remw
        sb
        sbreak
        scall
        sc_d
        sc_w
        sd
        sh
        sll
        slli
        slliw
        sllw
        slt
        slti
        sltiu
        sltu
        sra
        srai
        sraiw
        sraw
        sret
        srl
        srli
        srliw
        srlw
        sub
        subw
        sw
        xor
        xori
)

set(riscv_gen_icache_h ${CMAKE_CURRENT_BINARY_DIR}/icache.h)

add_custom_command(
        OUTPUT ${riscv_gen_icache_h}
        COMMAND ${SCRIPT_DIR}/code_gen.sh
        ARGS icache ${riscv_gen_icache_h} ${CMAKE_CURRENT_SOURCE_DIR}/gen_icache ${CMAKE_CURRENT_SOURCE_DIR}/mmu.h
        DEPENDS ${SCRIPT_DIR}/code_gen.sh ${CMAKE_CURRENT_SOURCE_DIR}/gen_icache ${CMAKE_CURRENT_SOURCE_DIR}/mmu.h
        VERBATIM
)

macro(riscv_insn_srcs_generator outfiles)
    foreach (i ${ARGN})
        set(i_out ${CMAKE_CURRENT_BINARY_DIR}/${i}.cc)
        add_custom_command(
                OUTPUT ${i_out}
                COMMAND ${SCRIPT_DIR}/code_gen.sh
                ARGS insn_src ${i_out} ${i} ${CMAKE_CURRENT_SOURCE_DIR}/encoding.h ${CMAKE_CURRENT_SOURCE_DIR}/insn_template.cc
                DEPENDS ${SCRIPT_DIR}/code_gen.sh ${CMAKE_CURRENT_SOURCE_DIR}/encoding.h ${CMAKE_CURRENT_SOURCE_DIR}/insn_template.cc
        )
        set(${outfiles} ${${outfiles}} ${i_out})
    endforeach (i)
    include_directories(${CMAKE_CURRENT_BINARY_DIR})
endmacro(riscv_insn_srcs_generator)

riscv_insn_srcs_generator(riscv_gen_insn_src_list ${riscv_insn_list})

string(REPLACE ";" " " space_sep_insn_list "${riscv_insn_list}")
message("Supported instruction list: ${space_sep_insn_list}")

set(
        riscv_gen_srcs
        ${riscv_gen_insn_src_list}
)

set(
        riscv_gen_hdrs
        ${riscv_gen_icache_h}
)
