/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1987 Gary W. Ng
**********/

#include "ngspice/ngspice.h"
#include "bjtdefs.h"
#include "ngspice/cktdefs.h"
#include "ngspice/iferrmsg.h"
#include "ngspice/noisedef.h"
#include "ngspice/suffix.h"

/*
*BJTnoise (mode, operation, firstModel, ckt, data, OnDens)
 *
*   This routine names and evaluates all of the noise sources
*   associated with BJT's.  It starts with the model *firstModel and
*   traverses all of its insts.  It then proceeds to any other models
*   on the linked list.  The total output noise density generated by
*   all of the BJT's is summed with the variable "OnDens".
 */

int
BJTnoise(int mode, int operation, GENmodel*genmodel, CKTcircuit *ckt,
         Ndata *data, double *OnDens)
{
    NOISEAN *job = (NOISEAN*) ckt->CKTcurJob;

    BJTmodel *firstModel = (BJTmodel*) genmodel;
    BJTmodel *model;
    BJTinstance *inst;
    double tempOnoise;
    double tempInoise;
    double noizDens[BJTNSRCS];
    double lnNdens[BJTNSRCS];
    int i;
    double dtemp;

    /* define the names of the noise sources */

    static char *BJTnNames[BJTNSRCS] = {
        /* Note that we have to keep the order consistent with the
           strchr definitions in BJTdefs.h */
        "_rc",        /* noise due to rc */
        "_rb",        /* noise due to rb */
        "_re",        /* noise due to re */
        "_ic",        /* noise due to ic */
        "_ib",        /* noise due to ib */
        "_1overf",    /* flicker (1/f) noise */
        ""            /* total transistor noise */
    };

    for (model = firstModel; model != NULL; model = BJTnextModel(model)) {
        for (inst = BJTinstances(model); inst != NULL; inst = BJTnextInstance(inst)) {

            switch (operation) {

            case N_OPEN:

                /* see if we have to to produce a summary report */
                /* if so, name all the noise generators */

                if (job->NStpsSm != 0) {
                    switch (mode) {

                    case N_DENS:
                        for (i = 0; i < BJTNSRCS; i++) {
                            NOISE_ADD_OUTVAR(ckt, data, "onoise_%s%s", inst->BJTname, BJTnNames[i]);
                        }
                        break;

                    case INT_NOIZ:
                        for (i = 0; i < BJTNSRCS; i++) {
                            NOISE_ADD_OUTVAR(ckt, data, "onoise_total_%s%s", inst->BJTname, BJTnNames[i]);
                            NOISE_ADD_OUTVAR(ckt, data, "inoise_total_%s%s", inst->BJTname, BJTnNames[i]);
                        }
                        break;
                    }
                }
                break;

            case N_CALC:
                switch (mode) {

                case N_DENS:

                    if (inst->BJTtempGiven)
                        dtemp = inst->BJTtemp - ckt->CKTtemp + (model->BJTtnom-CONSTCtoK);
                    else
                        dtemp = inst->BJTdtemp;

                    NevalSrcInstanceTemp(&noizDens[BJTRCNOIZ],&lnNdens[BJTRCNOIZ],
                        ckt, THERMNOISE, inst->BJTcollCXNode, inst->BJTcolNode,
                        inst->BJTtcollectorConduct * inst->BJTm, dtemp);

                    NevalSrcInstanceTemp(&noizDens[BJTRBNOIZ],&lnNdens[BJTRBNOIZ],
                        ckt, THERMNOISE, inst->BJTbasePrimeNode, inst->BJTbaseNode,
                        *(ckt->CKTstate0 + inst->BJTgx) * inst->BJTm, dtemp);

                    NevalSrcInstanceTemp(&noizDens[BJT_RE_NOISE],&lnNdens[BJT_RE_NOISE],
                        ckt, THERMNOISE, inst->BJTemitPrimeNode, inst->BJTemitNode,
                        inst->BJTtemitterConduct * inst->BJTm, dtemp);

                    NevalSrc(&noizDens[BJTICNOIZ],&lnNdens[BJTICNOIZ],
                        ckt, SHOTNOISE, inst->BJTcolPrimeNode, inst->BJTemitPrimeNode,
                        *(ckt->CKTstate0 + inst->BJTcc) * inst->BJTm);

                    NevalSrc(&noizDens[BJTIBNOIZ],&lnNdens[BJTIBNOIZ],
                        ckt, SHOTNOISE, inst->BJTbasePrimeNode, inst->BJTemitPrimeNode,
                        *(ckt->CKTstate0 + inst->BJTcb) * inst->BJTm);

                    NevalSrc(&noizDens[BJTFLNOIZ], NULL, ckt,
                        N_GAIN, inst->BJTbasePrimeNode, inst->BJTemitPrimeNode,
                        (double) 0.0);
                    noizDens[BJTFLNOIZ] *= inst->BJTm * model->BJTfNcoef *
                        exp(model->BJTfNexp *
                            log(MAX(fabs(*(ckt->CKTstate0 + inst->BJTcb)), N_MINLOG))) /
                        data->freq;
                    lnNdens[BJTFLNOIZ] =
                        log(MAX(noizDens[BJTFLNOIZ], N_MINLOG));

                    noizDens[BJTTOTNOIZ] = noizDens[BJTRCNOIZ] +
                        noizDens[BJTRBNOIZ] +
                        noizDens[BJT_RE_NOISE] +
                        noizDens[BJTICNOIZ] +
                        noizDens[BJTIBNOIZ] +
                        noizDens[BJTFLNOIZ];
                    lnNdens[BJTTOTNOIZ] =
                        log(noizDens[BJTTOTNOIZ]);

                   *OnDens += noizDens[BJTTOTNOIZ];

                    if (data->delFreq == 0.0) {

                        /* if we haven't done any previous integration, we need to */
                        /* initialize our "history" variables                      */

                        for (i = 0; i < BJTNSRCS; i++) {
                            inst->BJTnVar[LNLSTDENS][i] = lnNdens[i];
                        }

                        /* clear out our integration variables if it's the first pass */

                        if (data->freq == job->NstartFreq) {
                            for (i = 0; i < BJTNSRCS; i++) {
                                inst->BJTnVar[OUTNOIZ][i] = 0.0;
                                inst->BJTnVar[INNOIZ][i] = 0.0;
                            }
                        }
                    } else {
                        /* data->delFreq != 0.0 (we have to integrate) */

                        /* In order to get the best curve fit, we have to integrate each component separately */

                        for (i = 0; i < BJTNSRCS; i++) {
                            if (i != BJTTOTNOIZ) {
                                tempOnoise = Nintegrate(noizDens[i], lnNdens[i],
                                    inst->BJTnVar[LNLSTDENS][i], data);
                                tempInoise = Nintegrate(noizDens[i] * data->GainSqInv,
                                    lnNdens[i] + data->lnGainInv,
                                    inst->BJTnVar[LNLSTDENS][i] + data->lnGainInv,
                                    data);
                                inst->BJTnVar[LNLSTDENS][i] = lnNdens[i];
                                data->outNoiz += tempOnoise;
                                data->inNoise += tempInoise;
                                if (job->NStpsSm != 0) {
                                    inst->BJTnVar[OUTNOIZ][i] += tempOnoise;
                                    inst->BJTnVar[OUTNOIZ][BJTTOTNOIZ] += tempOnoise;
                                    inst->BJTnVar[INNOIZ][i] += tempInoise;
                                    inst->BJTnVar[INNOIZ][BJTTOTNOIZ] += tempInoise;
                                }
                            }
                        }
                    }
                    if (data->prtSummary) {
                        for (i = 0; i < BJTNSRCS; i++) {
                            /* print a summary report */
                            data->outpVector[data->outNumber++] = noizDens[i];
                        }
                    }
                    break;

                case INT_NOIZ:
                    /* already calculated, just output */
                    if (job->NStpsSm != 0) {
                        for (i = 0; i < BJTNSRCS; i++) {
                            data->outpVector[data->outNumber++] = inst->BJTnVar[OUTNOIZ][i];
                            data->outpVector[data->outNumber++] = inst->BJTnVar[INNOIZ][i];
                        }
                    } /* if */
                    break;
                } /* switch (mode) */
                break;

            case N_CLOSE:
                return (OK); /* do nothing, the main calling routine will close */
                break; /* the plots */
            } /* switch (operation) */
        } /* for inst */
    } /* for model */

    return (OK);
}