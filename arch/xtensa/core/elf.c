/*
 * Copyright (c) 2023 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/llext/elf.h>
#include <zephyr/llext/llext.h>
#include <zephyr/llext/llext_internal.h>
#include <zephyr/llext/loader.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(llext);

#define R_XTENSA_NONE           0
#define R_XTENSA_32             1
#define R_XTENSA_RTLD           2
#define R_XTENSA_GLOB_DAT       3
#define R_XTENSA_JMP_SLOT       4
#define R_XTENSA_RELATIVE       5
#define R_XTENSA_PLT            6
#define R_XTENSA_SLOT0_OP	20

/**
 * @brief Architecture specific function for relocating shared elf
 *
 * Elf files contain a series of relocations described in multiple sections.
 * These relocation instructions are architecture specific and each architecture
 * supporting modules must implement this.
 */
void arch_elf_relocate_local(struct llext_loader *ldr, struct llext *ext,
			     const elf_rela_t *rel, const elf_sym_t *sym, size_t got_offset)
{
	uint8_t *text = ext->mem[LLEXT_MEM_TEXT];
	int type = ELF32_R_TYPE(rel->r_info);
	elf_word *got_entry = (elf_word *)(text + got_offset);
	uintptr_t sh_addr;

	if (ELF_ST_TYPE(sym->st_info) == STT_SECTION) {
		elf_shdr_t *shdr = llext_peek(ldr, ldr->hdr.e_shoff +
					      sym->st_shndx * ldr->hdr.e_shentsize);
		sh_addr = shdr->sh_addr ? : (uintptr_t)llext_peek(ldr, shdr->sh_offset);
	} else {
		sh_addr = ldr->sects[LLEXT_MEM_TEXT].sh_addr;
	}

	switch (type) {
	case R_XTENSA_RELATIVE:
		/* Relocate a local symbol: Xtensa specific */
		*got_entry += (uintptr_t)text - sh_addr;
		break;
	case R_XTENSA_32:
		*got_entry += sh_addr;
		break;
	case R_XTENSA_SLOT0_OP:
		;
		uint8_t *opc = (uint8_t *)got_entry;

		/* Check the opcode: is this an L32R? And does it have to be relocated? */
		if ((opc[0] & 0xf) != 1 || opc[1] || opc[2])
			break;

		elf_sym_t rsym;

		int ret = llext_seek(ldr, ldr->sects[LLEXT_MEM_SYMTAB].sh_offset +
				     ELF_R_SYM(rel->r_info) * sizeof(elf_sym_t));
		if (!ret) {
			ret = llext_read(ldr, &rsym, sizeof(elf_sym_t));
		}
		if (ret)
			return;

		uintptr_t link_addr = (uintptr_t)llext_loaded_sect_ptr(ldr, ext, rsym.st_shndx) +
			rsym.st_value + rel->r_addend;

		ssize_t value = (link_addr - (((uintptr_t)got_entry + 3) & ~3)) >> 2;

		opc[1] = value & 0xff;
		opc[2] = (value >> 8) & 0xff;

		break;
	default:
		LOG_DBG("unsupported relocation type %u", type);

		return;
	}

	LOG_DBG("relocation to %#x type %u at %p", *got_entry, type, (void *)got_entry);
}
