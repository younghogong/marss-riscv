/**
 * Branch Prediction Unit
 *
 * MARSS-RISCV : Micro-Architectural System Simulator for RISC-V
 *
 * Copyright (c) 2017-2019 Gaurav Kothari {gkothar1@binghamton.edu}
 * State University of New York at Binghamton
 *
 * Copyright (c) 2018-2019 Parikshit Sarnaik {psarnai1@binghamton.edu}
 * State University of New York at Binghamton
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "bpu.h"

#define PRED_NOT_TAKEN 0x0
#define PRED_TAKEN 0x1
#define BPU_MISS 0x0
#define BPU_HIT 0x1

BranchPredUnit *
bpu_init(const SimParams *p, SimStats *s)
{
    BranchPredUnit *u;

    u = (BranchPredUnit *)calloc(1, sizeof(BranchPredUnit));
    assert(u);
    u->btb = NULL;
    u->ap = NULL;
    u->stats = s;
    u->btb = btb_init(p);

    /* bpu_type with value 0 uses bimodal predictor */
    if (p->bpu_type)
    {
        u->ap = adaptive_predictor_init(p);
    }

    return u;
}

void
bpu_flush(BranchPredUnit *u)
{
    btb_flush(u->btb);
    if (u->ap)
    {
        adaptive_predictor_flush(u->ap);
    }
}

void
bpu_free(BranchPredUnit **u)
{
    btb_free(&(*u)->btb);
    if ((*u)->ap)
    {
        adaptive_predictor_free(&(*u)->ap);
    }
    free(*u);
    *u = NULL;
}

/* Probes the BPU for given pc. */
void
bpu_probe(BranchPredUnit *u, target_ulong pc, BPUResponsePkt *p, int priv)
{
    p->ap_probe_status = BPU_HIT;
    p->btb_probe_status = btb_probe(u->btb, pc, &p->btb_entry);
    if (p->btb_probe_status == BPU_HIT)
    {
        ++(u->stats[priv].btb_hits);
    }
    ++(u->stats[priv].btb_probes);

    if (u->ap && ((p->btb_probe_status == BPU_MISS)
                  || (p->btb_entry->type == BRANCH_COND)))
    {
        p->ap_probe_status = adaptive_predictor_probe(u->ap, pc);
    }

    p->bpu_probe_status = p->btb_probe_status && p->ap_probe_status;
}

/**
 * Returns the target address for this pc. Predictions for conditional branches
 * are checked before returning the target address. If prediction is taken,target
 * address is returned, else 0 is returned.
 */
target_ulong
bpu_get_target(BranchPredUnit *u, target_ulong pc, BtbEntry *btb_entry)
{
    switch (btb_entry->type)
    {
        case BRANCH_UNCOND:
        {
            /* No need to check prediction for unconditional branches, so
               directly return target address. */
            return btb_entry->target;
        }

        case BRANCH_COND:
        {
            /* Must check prediction for conditional branches, so if prediction is taken
               return the target address, else return 0. */
            if (u->ap)
            {
                if (adaptive_predictor_get_prediction(u->ap, pc))
                {
                    return btb_entry->target;
                }
            }
            else
            {
                /* Bimodal */
                if (btb_entry->pred > 1)
                {
                    return btb_entry->target;
                }
            }

            /* BPU Hit, but prediction is not-taken */
            return 0;
        }
    }

    /* Control never reaches here */
    assert(0);
    return 0;
}

void
bpu_add(BranchPredUnit *u, target_ulong pc, int type, BPUResponsePkt *p, int priv)
{
    /* All the branches are allocated BTB entry */
    if (!p->btb_probe_status)
    {
        btb_add(u->btb, pc, type);
        ++(u->stats[priv].btb_inserts);
    }

    /* If BPU is using adaptive predictor, then PC must also be added in
       adaptive predictor structures, but only for conditional branches */
    if (u->ap && (type == BRANCH_COND))
    {
        if (!p->ap_probe_status)
        {
            adaptive_predictor_add(u->ap, pc);
        }
    }
}

void
bpu_update(BranchPredUnit *u, target_ulong pc, target_ulong target, int pred,
           int type, BPUResponsePkt *p, int priv)
{
    if (p->btb_probe_status)
    {
        btb_update(p->btb_entry, target, pred, type);
        ++(u->stats[priv].btb_updates);
    }

    /* If BPU is using adaptive predictor, adaptive predictor structures must be
       also be updated, but only for conditional branches */
    if (u->ap && (type == BRANCH_COND))
    {
        if (p->ap_probe_status)
        {
            adaptive_predictor_update(u->ap, pc, pred);
        }
    }
}
