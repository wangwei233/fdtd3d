#include "BMPDumper.h"
#include "BMPLoader.h"
#include "DATDumper.h"
#include "DATLoader.h"
#include "Kernels.h"
#include "SchemeTMz.h"

#if defined (PARALLEL_GRID)
#include <mpi.h>
#endif

#include <cmath>

#if defined (CUDA_ENABLED)
#include "CudaInterface.h"
#endif

#ifdef GRID_2D

void
SchemeTMz::performPlaneWaveESteps (time_step t)
{
  for (grid_coord i = 1; i < EInc.getSize ().getX (); ++i)
  {
    GridCoordinate1D pos (i);

    FieldPointValue *valE = EInc.getFieldPointValue (pos);

    GridCoordinate1D posLeft (i - 1);
    GridCoordinate1D posRight (i);

    FieldPointValue *valH1 = HInc.getFieldPointValue (posLeft);
    FieldPointValue *valH2 = HInc.getFieldPointValue (posRight);

    FieldValue S = gridTimeStep * PhysicsConst::SpeedOfLight / gridStep;
    FieldValue arg = PhysicsConst::Pi * S / stepWaveLength;

    FieldValue relPhi;
    if (incidentWaveAngle == PhysicsConst::Pi / 4)
    {
      relPhi = sqrt (2) * asin (sin(arg) / (S * sqrt (2))) / asin (sin (arg) / S);
    }
    else
    {
      ASSERT (incidentWaveAngle == 0);

      relPhi = 1;
    }

    FieldValue val = valE->getPrevValue () + (gridTimeStep / (relPhi * PhysicsConst::Eps0 * gridStep)) * (valH1->getPrevValue () - valH2->getPrevValue ());

    valE->setCurValue (val);
  }

  FieldValue freq = PhysicsConst::SpeedOfLight / waveLength;

  GridCoordinate1D pos (0);
  FieldPointValue *valE = EInc.getFieldPointValue (pos);
  valE->setCurValue (sin (gridTimeStep * t * 2 * PhysicsConst::Pi * freq));

  // grid_coord last_pos = EInc.getSize ().getX () - 2;
  //
  // if (t * gridTimeStep * freq >= last_pos / stepWaveLength)
  // {
  //   GridCoordinate1D pos (last_pos);
  //   FieldPointValue *valE = EInc.getFieldPointValue (pos);
  //   valE->setCurValue (sin (gridTimeStep * t * 2 * PhysicsConst::Pi * freq - 2 * PhysicsConst::Pi * last_pos / stepWaveLength));
  // }

  EInc.nextTimeStep ();
}

void
SchemeTMz::performPlaneWaveHSteps (time_step t)
{
  for (grid_coord i = 0; i < HInc.getSize ().getX () - 1; ++i)
  {
    GridCoordinate1D pos (i);

    FieldPointValue *valH = HInc.getFieldPointValue (pos);

    GridCoordinate1D posLeft (i);
    GridCoordinate1D posRight (i + 1);

    FieldPointValue *valE1 = EInc.getFieldPointValue (posLeft);
    FieldPointValue *valE2 = EInc.getFieldPointValue (posRight);

    FieldValue N_lambda = waveLength / gridStep;
    FieldValue S = gridTimeStep * PhysicsConst::SpeedOfLight / gridStep;
    FieldValue arg = PhysicsConst::Pi * S / N_lambda;

    FieldValue relPhi;
    if (incidentWaveAngle == PhysicsConst::Pi / 4)
    {
      relPhi = sqrt (2) * asin (sin(arg) / (S * sqrt (2))) / asin (sin (arg) / S);
    }
    else
    {
      ASSERT (incidentWaveAngle == 0);

      relPhi = 1;
    }

    FieldValue val = valH->getPrevValue () + (gridTimeStep / (relPhi * PhysicsConst::Mu0 * gridStep)) * (valE1->getPrevValue () - valE2->getPrevValue ());

    valH->setCurValue (val);
  }

  HInc.nextTimeStep ();
}

void
SchemeTMz::performEzSteps (time_step t, GridCoordinate3D EzStart, GridCoordinate3D EzEnd)
{
  /*
   * FIXME: check performed on each iteration
   */
  if (usePML)
  {
    calculateEzStepPML (t, EzStart, EzEnd);
  }
  else
  {
    calculateEzStep (t, EzStart, EzEnd);
  }
}

void
SchemeTMz::calculateEzStep (time_step t, GridCoordinate3D EzStart, GridCoordinate3D EzEnd)
{
  FieldValue eps0 = PhysicsConst::Eps0;

  for (int i = EzStart.getX (); i < EzEnd.getX (); ++i)
  {
    for (int j = EzStart.getY (); j < EzEnd.getY (); ++j)
    {
      GridCoordinate2D pos (i, j);

      GridCoordinate2D posAbs = Ez.getTotalPosition (pos);

      GridCoordinate2D posLeft = shrinkCoord (yeeLayout.getEzCircuitElement (GridCoordinate3D (pos), LayoutDirection::LEFT));
      GridCoordinate2D posRight = shrinkCoord (yeeLayout.getEzCircuitElement (GridCoordinate3D (pos), LayoutDirection::RIGHT));
      GridCoordinate2D posDown = shrinkCoord (yeeLayout.getEzCircuitElement (GridCoordinate3D (pos), LayoutDirection::DOWN));
      GridCoordinate2D posUp = shrinkCoord (yeeLayout.getEzCircuitElement (GridCoordinate3D (pos), LayoutDirection::UP));

      FieldPointValue* valEz = Ez.getFieldPointValue (pos);
      FieldPointValue* valEps = Eps.getFieldPointValue (pos);

      FieldPointValue* valHy1 = Hy.getFieldPointValue (posRight);
      FieldPointValue* valHy2 = Hy.getFieldPointValue (posLeft);

      FieldPointValue* valHx1 = Hx.getFieldPointValue (posUp);
      FieldPointValue* valHx2 = Hx.getFieldPointValue (posDown);

      FieldValue prevHx1 = valHx1->getPrevValue ();
      FieldValue prevHx2 = valHx2->getPrevValue ();
      FieldValue prevHy1 = valHy1->getPrevValue ();
      FieldValue prevHy2 = valHy2->getPrevValue ();

      if (useTFSF)
      {
        if (yeeLayout.doNeedTFSFUpdateEzBorder (posAbs, LayoutDirection::LEFT))
        {
          /*
           * HInc: 0, 1, etc.
           */
          GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getHyCoordFP (Hy.getTotalPosition (posRight)));
          GridCoordinateFP2D zeroCoordFP = yeeLayout.getZeroIncCoordFP ();
          FieldValue x = realCoord.getX () - zeroCoordFP.getX ();
          FieldValue y = realCoord.getY () - zeroCoordFP.getY ();
          FieldValue d = x * cos (incidentWaveAngle) + y * sin (incidentWaveAngle) - 0.5;
          FieldValue coordD1 = (FieldValue) ((grid_iter) d);
          FieldValue coordD2 = coordD1 + 1;
          FieldValue proportionD2 = d - coordD1;
          FieldValue proportionD1 = 1 - proportionD2;

          GridCoordinate1D pos1 (coordD1);
          GridCoordinate1D pos2 (coordD2);

          FieldPointValue *valH1 = HInc.getFieldPointValue (pos1);
          FieldPointValue *valH2 = HInc.getFieldPointValue (pos2);

          FieldValue diff = proportionD1 * valH1->getPrevValue () + proportionD2 * valH2->getPrevValue ();

          prevHy1 -= -diff * cos (incidentWaveAngle);
        }

        if (yeeLayout.doNeedTFSFUpdateEzBorder (posAbs, LayoutDirection::RIGHT))
        {
          GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getHyCoordFP (Hy.getTotalPosition (posLeft)));
          GridCoordinateFP2D zeroCoordFP = yeeLayout.getZeroIncCoordFP ();

          FieldValue x = realCoord.getX () - zeroCoordFP.getX ();
          FieldValue y = realCoord.getY () - zeroCoordFP.getY ();
          FieldValue d = x * cos (incidentWaveAngle) + y * sin (incidentWaveAngle) - 0.5;
          FieldValue coordD1 = (FieldValue) ((grid_iter) d);
          FieldValue coordD2 = coordD1 + 1;
          FieldValue proportionD2 = d - coordD1;
          FieldValue proportionD1 = 1 - proportionD2;

          GridCoordinate1D pos1 (coordD1);
          GridCoordinate1D pos2 (coordD2);

          FieldPointValue *valH1 = HInc.getFieldPointValue (pos1);
          FieldPointValue *valH2 = HInc.getFieldPointValue (pos2);

          FieldValue diff = proportionD1 * valH1->getPrevValue () + proportionD2 * valH2->getPrevValue ();

          prevHy2 -= -diff * cos (incidentWaveAngle);
        }

        if (yeeLayout.doNeedTFSFUpdateEzBorder (posAbs, LayoutDirection::DOWN))
        {
          GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getHxCoordFP (Hx.getTotalPosition (posUp)));
          GridCoordinateFP2D zeroCoordFP = yeeLayout.getZeroIncCoordFP ();

          FieldValue x = realCoord.getX () - zeroCoordFP.getX ();
          FieldValue y = realCoord.getY () - zeroCoordFP.getY ();
          FieldValue d = x * cos (incidentWaveAngle) + y * sin (incidentWaveAngle) - 0.5;
          FieldValue coordD1 = (FieldValue) ((grid_iter) d);
          FieldValue coordD2 = coordD1 + 1;
          FieldValue proportionD2 = d - coordD1;
          FieldValue proportionD1 = 1 - proportionD2;

          GridCoordinate1D pos1 (coordD1);
          GridCoordinate1D pos2 (coordD2);

          FieldPointValue *valH1 = HInc.getFieldPointValue (pos1);
          FieldPointValue *valH2 = HInc.getFieldPointValue (pos2);

          FieldValue diff = proportionD1 * valH1->getPrevValue () + proportionD2 * valH2->getPrevValue ();

          prevHx1 -= diff * sin (incidentWaveAngle);
        }

        if (yeeLayout.doNeedTFSFUpdateEzBorder (posAbs, LayoutDirection::UP))
        {
          GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getHxCoordFP (Hx.getTotalPosition (posDown)));
          GridCoordinateFP2D zeroCoordFP = yeeLayout.getZeroIncCoordFP ();

          FieldValue x = realCoord.getX () - zeroCoordFP.getX ();
          FieldValue y = realCoord.getY () - zeroCoordFP.getY ();
          FieldValue d = x * cos (incidentWaveAngle) + y * sin (incidentWaveAngle) - 0.5;
          FieldValue coordD1 = (FieldValue) ((grid_iter) d);
          FieldValue coordD2 = coordD1 + 1;
          FieldValue proportionD2 = d - coordD1;
          FieldValue proportionD1 = 1 - proportionD2;

          GridCoordinate1D pos1 (coordD1);
          GridCoordinate1D pos2 (coordD2);

          FieldPointValue *valH1 = HInc.getFieldPointValue (pos1);
          FieldPointValue *valH2 = HInc.getFieldPointValue (pos2);

          FieldValue diff = proportionD1 * valH1->getPrevValue () + proportionD2 * valH2->getPrevValue ();

          prevHx2 -= diff * sin (incidentWaveAngle);
        }
      }

      FieldValue val = calculateEz_3D (valEz->getPrevValue (),
                                       prevHy1,
                                       prevHy2,
                                       prevHx1,
                                       prevHx2,
                                       gridTimeStep,
                                       gridStep,
                                       valEps->getCurValue () * eps0);

      valEz->setCurValue (val);
    }
  }
}

void
SchemeTMz::calculateEzStepPML (time_step t, GridCoordinate3D EzStart, GridCoordinate3D EzEnd)
{
  FieldValue eps0 = PhysicsConst::Eps0;

  for (int i = EzStart.getX (); i < EzEnd.getX (); ++i)
  {
    for (int j = EzStart.getY (); j < EzEnd.getY (); ++j)
    {
      GridCoordinate2D pos (i, j);

      GridCoordinate2D posAbs = Ez.getTotalPosition (pos);

      GridCoordinate2D posLeft = shrinkCoord (yeeLayout.getEzCircuitElement (GridCoordinate3D (pos), LayoutDirection::LEFT));
      GridCoordinate2D posRight = shrinkCoord (yeeLayout.getEzCircuitElement (GridCoordinate3D (pos), LayoutDirection::RIGHT));
      GridCoordinate2D posDown = shrinkCoord (yeeLayout.getEzCircuitElement (GridCoordinate3D (pos), LayoutDirection::DOWN));
      GridCoordinate2D posUp = shrinkCoord (yeeLayout.getEzCircuitElement (GridCoordinate3D (pos), LayoutDirection::UP));

      FieldPointValue* valDz = Dz.getFieldPointValue (pos);

      FieldPointValue* valSigmaX = SigmaX.getFieldPointValue (pos);

      FieldPointValue* valHy1 = Hy.getFieldPointValue (posRight);
      FieldPointValue* valHy2 = Hy.getFieldPointValue (posLeft);

      FieldPointValue* valHx1 = Hx.getFieldPointValue (posUp);
      FieldPointValue* valHx2 = Hx.getFieldPointValue (posDown);

      FieldValue prevHx1 = valHx1->getPrevValue ();
      FieldValue prevHx2 = valHx2->getPrevValue ();
      FieldValue prevHy1 = valHy1->getPrevValue ();
      FieldValue prevHy2 = valHy2->getPrevValue ();

      if (useTFSF)
      {
        if (yeeLayout.doNeedTFSFUpdateEzBorder (posAbs, LayoutDirection::LEFT))
        {
          /*
           * HInc: 0, 1, etc.
           */
          GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getHyCoordFP (Hy.getTotalPosition (posRight)));
          GridCoordinateFP2D zeroCoordFP = yeeLayout.getZeroIncCoordFP ();
          FieldValue x = realCoord.getX () - zeroCoordFP.getX ();
          FieldValue y = realCoord.getY () - zeroCoordFP.getY ();
          FieldValue d = x * cos (incidentWaveAngle) + y * sin (incidentWaveAngle) - 0.5;
          FieldValue coordD1 = (FieldValue) ((grid_iter) d);
          FieldValue coordD2 = coordD1 + 1;
          FieldValue proportionD2 = d - coordD1;
          FieldValue proportionD1 = 1 - proportionD2;

          GridCoordinate1D pos1 (coordD1);
          GridCoordinate1D pos2 (coordD2);

          FieldPointValue *valH1 = HInc.getFieldPointValue (pos1);
          FieldPointValue *valH2 = HInc.getFieldPointValue (pos2);

          FieldValue diff = proportionD1 * valH1->getPrevValue () + proportionD2 * valH2->getPrevValue ();

          prevHy1 -= -diff * cos (incidentWaveAngle);
        }

        if (yeeLayout.doNeedTFSFUpdateEzBorder (posAbs, LayoutDirection::RIGHT))
        {
          GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getHyCoordFP (Hy.getTotalPosition (posLeft)));
          GridCoordinateFP2D zeroCoordFP = yeeLayout.getZeroIncCoordFP ();

          FieldValue x = realCoord.getX () - zeroCoordFP.getX ();
          FieldValue y = realCoord.getY () - zeroCoordFP.getY ();
          FieldValue d = x * cos (incidentWaveAngle) + y * sin (incidentWaveAngle) - 0.5;
          FieldValue coordD1 = (FieldValue) ((grid_iter) d);
          FieldValue coordD2 = coordD1 + 1;
          FieldValue proportionD2 = d - coordD1;
          FieldValue proportionD1 = 1 - proportionD2;

          GridCoordinate1D pos1 (coordD1);
          GridCoordinate1D pos2 (coordD2);

          FieldPointValue *valH1 = HInc.getFieldPointValue (pos1);
          FieldPointValue *valH2 = HInc.getFieldPointValue (pos2);

          FieldValue diff = proportionD1 * valH1->getPrevValue () + proportionD2 * valH2->getPrevValue ();

          prevHy2 -= -diff * cos (incidentWaveAngle);
        }

        if (yeeLayout.doNeedTFSFUpdateEzBorder (posAbs, LayoutDirection::DOWN))
        {
          GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getHxCoordFP (Hx.getTotalPosition (posUp)));
          GridCoordinateFP2D zeroCoordFP = yeeLayout.getZeroIncCoordFP ();

          FieldValue x = realCoord.getX () - zeroCoordFP.getX ();
          FieldValue y = realCoord.getY () - zeroCoordFP.getY ();
          FieldValue d = x * cos (incidentWaveAngle) + y * sin (incidentWaveAngle) - 0.5;
          FieldValue coordD1 = (FieldValue) ((grid_iter) d);
          FieldValue coordD2 = coordD1 + 1;
          FieldValue proportionD2 = d - coordD1;
          FieldValue proportionD1 = 1 - proportionD2;

          GridCoordinate1D pos1 (coordD1);
          GridCoordinate1D pos2 (coordD2);

          FieldPointValue *valH1 = HInc.getFieldPointValue (pos1);
          FieldPointValue *valH2 = HInc.getFieldPointValue (pos2);

          FieldValue diff = proportionD1 * valH1->getPrevValue () + proportionD2 * valH2->getPrevValue ();

          prevHx1 -= diff * sin (incidentWaveAngle);
        }

        if (yeeLayout.doNeedTFSFUpdateEzBorder (posAbs, LayoutDirection::UP))
        {
          GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getHxCoordFP (Hx.getTotalPosition (posDown)));
          GridCoordinateFP2D zeroCoordFP = yeeLayout.getZeroIncCoordFP ();

          FieldValue x = realCoord.getX () - zeroCoordFP.getX ();
          FieldValue y = realCoord.getY () - zeroCoordFP.getY ();
          FieldValue d = x * cos (incidentWaveAngle) + y * sin (incidentWaveAngle) - 0.5;
          FieldValue coordD1 = (FieldValue) ((grid_iter) d);
          FieldValue coordD2 = coordD1 + 1;
          FieldValue proportionD2 = d - coordD1;
          FieldValue proportionD1 = 1 - proportionD2;

          GridCoordinate1D pos1 (coordD1);
          GridCoordinate1D pos2 (coordD2);

          FieldPointValue *valH1 = HInc.getFieldPointValue (pos1);
          FieldPointValue *valH2 = HInc.getFieldPointValue (pos2);

          FieldValue diff = proportionD1 * valH1->getPrevValue () + proportionD2 * valH2->getPrevValue ();

          prevHx2 -= diff * sin (incidentWaveAngle);
        }
      }

      /*
       * FIXME: precalculate coefficients
       */
      FieldValue k_x = 1;

      FieldValue Ca = (2 * eps0 * k_x - valSigmaX->getCurValue () * gridTimeStep) / (2 * eps0 * k_x + valSigmaX->getCurValue () * gridTimeStep);
      FieldValue Cb = (2 * eps0 * gridTimeStep / gridStep) / (2 * eps0 * k_x + valSigmaX->getCurValue () * gridTimeStep);

      FieldValue val = calculateEz_3D_Precalc (valDz->getPrevValue (),
                                               prevHy1,
                                               prevHy2,
                                               prevHx1,
                                               prevHx2,
                                               Ca,
                                               Cb);

      valDz->setCurValue (val);
    }
  }

  for (int i = EzStart.getX (); i < EzEnd.getX (); ++i)
  {
    for (int j = EzStart.getY (); j < EzEnd.getY (); ++j)
    {
      GridCoordinate2D pos (i, j);

      FieldPointValue* valEz = Ez.getFieldPointValue (pos);

      FieldPointValue* valDz = Dz.getFieldPointValue (pos);

      FieldPointValue* valSigmaY = SigmaY.getFieldPointValue (pos);
      FieldPointValue* valSigmaZ = SigmaZ.getFieldPointValue (pos);
      FieldPointValue* valEps = Eps.getFieldPointValue (pos);

      FieldValue k_y = 1;
      FieldValue k_z = 1;

      FieldValue Ca = (2 * eps0 * k_y - valSigmaY->getCurValue () * gridTimeStep) / (2 * eps0 * k_y + valSigmaY->getCurValue () * gridTimeStep);
      FieldValue Cb = ((2 * eps0 * k_z + valSigmaZ->getCurValue () * gridTimeStep) / (valEps->getCurValue () * eps0)) / (2 * eps0 * k_y + valSigmaY->getCurValue () * gridTimeStep);
      FieldValue Cc = ((2 * eps0 * k_z - valSigmaZ->getCurValue () * gridTimeStep) / (valEps->getCurValue () * eps0)) / (2 * eps0 * k_y + valSigmaY->getCurValue () * gridTimeStep);

      FieldValue val = calculateEz_from_Dz_Precalc (valEz->getPrevValue (),
                                                    valDz->getCurValue (),
                                                    valDz->getPrevValue (),
                                                    Ca,
                                                    Cb,
                                                    Cc);

      valEz->setCurValue (val);
    }
  }
}

void
SchemeTMz::performHxSteps (time_step t, GridCoordinate3D HxStart, GridCoordinate3D HxEnd)
{
  /*
   * FIXME: check performed on each iteration
   */
  if (usePML)
  {
    calculateHxStepPML (t, HxStart, HxEnd);
  }
  else
  {
    calculateHxStep (t, HxStart, HxEnd);
  }
}

void
SchemeTMz::calculateHxStep (time_step t, GridCoordinate3D HxStart, GridCoordinate3D HxEnd)
{
  FieldValue mu0 = PhysicsConst::Mu0;

  for (int i = HxStart.getX (); i < HxEnd.getX (); ++i)
  {
    for (int j = HxStart.getY (); j < HxEnd.getY (); ++j)
    {
      GridCoordinate2D pos (i, j);

      GridCoordinate2D posAbs = Hx.getTotalPosition (pos);

      GridCoordinate2D posDown = shrinkCoord (yeeLayout.getHxCircuitElement (pos, LayoutDirection::DOWN));
      GridCoordinate2D posUp = shrinkCoord (yeeLayout.getHxCircuitElement (pos, LayoutDirection::UP));

      FieldPointValue* valHx = Hx.getFieldPointValue (pos);
      FieldPointValue* valMu = Mu.getFieldPointValue (pos);

      FieldPointValue* valEz1 = Ez.getFieldPointValue (posUp);
      FieldPointValue* valEz2 = Ez.getFieldPointValue (posDown);

      FieldValue prevEz1 = valEz1->getPrevValue ();
      FieldValue prevEz2 = valEz2->getPrevValue ();

      if (useTFSF)
      {
        if (yeeLayout.doNeedTFSFUpdateHxBorder (posAbs, LayoutDirection::DOWN))
        {
          GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getEzCoordFP (Ez.getTotalPosition (posDown)));
          GridCoordinateFP2D zeroCoordFP = yeeLayout.getZeroIncCoordFP ();

          FieldValue x = realCoord.getX () - zeroCoordFP.getX ();
          FieldValue y = realCoord.getY () - zeroCoordFP.getY ();
          FieldValue d = x * cos (incidentWaveAngle) + y * sin (incidentWaveAngle);
          FieldValue coordD1 = (FieldValue) ((grid_iter) d);
          FieldValue coordD2 = coordD1 + 1;
          FieldValue proportionD2 = d - coordD1;
          FieldValue proportionD1 = 1 - proportionD2;

          GridCoordinate1D pos1 (coordD1);
          GridCoordinate1D pos2 (coordD2);

          FieldPointValue *valE1 = EInc.getFieldPointValue (pos1);
          FieldPointValue *valE2 = EInc.getFieldPointValue (pos2);

          FieldValue diff = proportionD1 * valE1->getPrevValue () + proportionD2 * valE2->getPrevValue ();

          prevEz2 += diff;
        }
        else if (yeeLayout.doNeedTFSFUpdateHxBorder (posAbs, LayoutDirection::UP))
        {
          GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getEzCoordFP (Ez.getTotalPosition (posUp)));
          GridCoordinateFP2D zeroCoordFP = yeeLayout.getZeroIncCoordFP ();

          FieldValue x = realCoord.getX () - zeroCoordFP.getX ();
          FieldValue y = realCoord.getY () - zeroCoordFP.getY ();
          FieldValue d = x * cos (incidentWaveAngle) + y * sin (incidentWaveAngle);
          FieldValue coordD1 = (FieldValue) ((grid_iter) d);
          FieldValue coordD2 = coordD1 + 1;
          FieldValue proportionD2 = d - coordD1;
          FieldValue proportionD1 = 1 - proportionD2;

          GridCoordinate1D pos1 (coordD1);
          GridCoordinate1D pos2 (coordD2);

          FieldPointValue *valE1 = EInc.getFieldPointValue (pos1);
          FieldPointValue *valE2 = EInc.getFieldPointValue (pos2);

          FieldValue diff = proportionD1 * valE1->getPrevValue () + proportionD2 * valE2->getPrevValue ();

          prevEz1 += diff;
        }
      }

      FieldValue val = calculateHx_2D_TMz (valHx->getPrevValue (),
                                           prevEz1,
                                           prevEz2,
                                           gridTimeStep,
                                           gridStep,
                                           valMu->getCurValue () * mu0);

      valHx->setCurValue (val);
    }
  }
}

void
SchemeTMz::calculateHxStepPML (time_step t, GridCoordinate3D HxStart, GridCoordinate3D HxEnd)
{
  FieldValue eps0 = PhysicsConst::Eps0;
  FieldValue mu0 = PhysicsConst::Mu0;

  for (int i = HxStart.getX (); i < HxEnd.getX (); ++i)
  {
    for (int j = HxStart.getY (); j < HxEnd.getY (); ++j)
    {
      GridCoordinate2D pos (i, j);

      GridCoordinate2D posAbs = Hx.getTotalPosition (pos);

      GridCoordinate2D posDown = shrinkCoord (yeeLayout.getHxCircuitElement (pos, LayoutDirection::DOWN));
      GridCoordinate2D posUp = shrinkCoord (yeeLayout.getHxCircuitElement (pos, LayoutDirection::UP));

      FieldPointValue* valBx = Bx.getFieldPointValue (pos);

      FieldPointValue* valSigmaY = SigmaY.getFieldPointValue (pos);

      FieldPointValue* valEz1 = Ez.getFieldPointValue (posUp);
      FieldPointValue* valEz2 = Ez.getFieldPointValue (posDown);

      FieldValue prevEz1 = valEz1->getPrevValue ();
      FieldValue prevEz2 = valEz2->getPrevValue ();

      if (useTFSF)
      {
        if (yeeLayout.doNeedTFSFUpdateHxBorder (posAbs, LayoutDirection::DOWN))
        {
          GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getEzCoordFP (Ez.getTotalPosition (posDown)));
          GridCoordinateFP2D zeroCoordFP = yeeLayout.getZeroIncCoordFP ();

          FieldValue x = realCoord.getX () - zeroCoordFP.getX ();
          FieldValue y = realCoord.getY () - zeroCoordFP.getY ();
          FieldValue d = x * cos (incidentWaveAngle) + y * sin (incidentWaveAngle);
          FieldValue coordD1 = (FieldValue) ((grid_iter) d);
          FieldValue coordD2 = coordD1 + 1;
          FieldValue proportionD2 = d - coordD1;
          FieldValue proportionD1 = 1 - proportionD2;

          GridCoordinate1D pos1 (coordD1);
          GridCoordinate1D pos2 (coordD2);

          FieldPointValue *valE1 = EInc.getFieldPointValue (pos1);
          FieldPointValue *valE2 = EInc.getFieldPointValue (pos2);

          FieldValue diff = proportionD1 * valE1->getPrevValue () + proportionD2 * valE2->getPrevValue ();

          prevEz2 += diff;
        }
        else if (yeeLayout.doNeedTFSFUpdateHxBorder (posAbs, LayoutDirection::UP))
        {
          GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getEzCoordFP (Ez.getTotalPosition (posUp)));
          GridCoordinateFP2D zeroCoordFP = yeeLayout.getZeroIncCoordFP ();

          FieldValue x = realCoord.getX () - zeroCoordFP.getX ();
          FieldValue y = realCoord.getY () - zeroCoordFP.getY ();
          FieldValue d = x * cos (incidentWaveAngle) + y * sin (incidentWaveAngle);
          FieldValue coordD1 = (FieldValue) ((grid_iter) d);
          FieldValue coordD2 = coordD1 + 1;
          FieldValue proportionD2 = d - coordD1;
          FieldValue proportionD1 = 1 - proportionD2;

          GridCoordinate1D pos1 (coordD1);
          GridCoordinate1D pos2 (coordD2);

          FieldPointValue *valE1 = EInc.getFieldPointValue (pos1);
          FieldPointValue *valE2 = EInc.getFieldPointValue (pos2);

          FieldValue diff = proportionD1 * valE1->getPrevValue () + proportionD2 * valE2->getPrevValue ();

          prevEz1 += diff;
        }
      }

      FieldValue k_y = 1;

      FieldValue Ca = (2 * eps0 * k_y - valSigmaY->getCurValue () * gridTimeStep) / (2 * eps0 * k_y + valSigmaY->getCurValue () * gridTimeStep);
      FieldValue Cb = (2 * eps0 * gridTimeStep / gridStep) / (2 * eps0 * k_y + valSigmaY->getCurValue () * gridTimeStep);

      FieldValue val = calculateHx_2D_TMz_Precalc (valBx->getPrevValue (),
                                                   prevEz1,
                                                   prevEz2,
                                                   Ca,
                                                   Cb);

      valBx->setCurValue (val);
    }
  }

  for (int i = HxStart.getX (); i < HxEnd.getX (); ++i)
  {
    for (int j = HxStart.getY (); j < HxEnd.getY (); ++j)
    {
      GridCoordinate2D pos (i, j);

      FieldPointValue* valHx = Hx.getFieldPointValue (pos);

      FieldPointValue* valBx = Bx.getFieldPointValue (pos);

      FieldPointValue* valSigmaX = SigmaX.getFieldPointValue (pos);
      FieldPointValue* valSigmaZ = SigmaZ.getFieldPointValue (pos);

      FieldPointValue* valMu = Mu.getFieldPointValue (pos);

      FieldValue k_x = 1;
      FieldValue k_z = 1;

      FieldValue Ca = (2 * eps0 * k_z - valSigmaZ->getCurValue () * gridTimeStep) / (2 * eps0 * k_z + valSigmaZ->getCurValue () * gridTimeStep);
      FieldValue Cb = ((2 * eps0 * k_x + valSigmaX->getCurValue () * gridTimeStep) / (valMu->getCurValue () * mu0)) / (2 * eps0 * k_z + valSigmaZ->getCurValue () * gridTimeStep);
      FieldValue Cc = ((2 * eps0 * k_x - valSigmaX->getCurValue () * gridTimeStep) / (valMu->getCurValue () * mu0)) / (2 * eps0 * k_z + valSigmaZ->getCurValue () * gridTimeStep);

      FieldValue val = calculateHx_from_Bx_Precalc (valHx->getPrevValue (),
                                                    valBx->getCurValue (),
                                                    valBx->getPrevValue (),
                                                    Ca,
                                                    Cb,
                                                    Cc);

      valHx->setCurValue (val);
    }
  }
}

void
SchemeTMz::performHySteps (time_step t, GridCoordinate3D HyStart, GridCoordinate3D HyEnd)
{
  /*
   * FIXME: check performed on each iteration
   */
  if (usePML)
  {
    calculateHyStepPML (t, HyStart, HyEnd);
  }
  else
  {
    calculateHyStep (t, HyStart, HyEnd);
  }
}

void
SchemeTMz::calculateHyStep (time_step t, GridCoordinate3D HyStart, GridCoordinate3D HyEnd)
{
  FieldValue mu0 = PhysicsConst::Mu0;

  for (int i = HyStart.getX (); i < HyEnd.getX (); ++i)
  {
    for (int j = HyStart.getY (); j < HyEnd.getY (); ++j)
    {
      GridCoordinate2D pos (i, j);

      GridCoordinate2D posAbs = Hy.getTotalPosition (pos);

      GridCoordinate2D posLeft = shrinkCoord (yeeLayout.getHyCircuitElement (pos, LayoutDirection::LEFT));
      GridCoordinate2D posRight = shrinkCoord (yeeLayout.getHyCircuitElement (pos, LayoutDirection::RIGHT));

      FieldPointValue* valHy = Hy.getFieldPointValue (pos);
      FieldPointValue* valMu = Mu.getFieldPointValue (pos);

      FieldPointValue* valEz1 = Ez.getFieldPointValue (posRight);
      FieldPointValue* valEz2 = Ez.getFieldPointValue (posLeft);

      FieldValue prevEz1 = valEz1->getPrevValue ();
      FieldValue prevEz2 = valEz2->getPrevValue ();

      if (useTFSF)
      {
        if (yeeLayout.doNeedTFSFUpdateHyBorder (posAbs, LayoutDirection::LEFT))
        {
          GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getEzCoordFP (Ez.getTotalPosition (posLeft)));
          GridCoordinateFP2D zeroCoordFP = yeeLayout.getZeroIncCoordFP ();

          FieldValue x = realCoord.getX () - zeroCoordFP.getX ();
          FieldValue y = realCoord.getY () - zeroCoordFP.getY ();
          FieldValue d = x * cos (incidentWaveAngle) + y * sin (incidentWaveAngle);
          FieldValue coordD1 = (FieldValue) ((grid_iter) d);
          FieldValue coordD2 = coordD1 + 1;
          FieldValue proportionD2 = d - coordD1;
          FieldValue proportionD1 = 1 - proportionD2;

          GridCoordinate1D pos1 (coordD1);
          GridCoordinate1D pos2 (coordD2);

          FieldPointValue *valE1 = EInc.getFieldPointValue (pos1);
          FieldPointValue *valE2 = EInc.getFieldPointValue (pos2);

          FieldValue diff = proportionD1 * valE1->getPrevValue () + proportionD2 * valE2->getPrevValue ();

          prevEz2 += diff;
        }
        else if (yeeLayout.doNeedTFSFUpdateHyBorder (posAbs, LayoutDirection::RIGHT))
        {
          GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getEzCoordFP (Ez.getTotalPosition (posRight)));
          GridCoordinateFP2D zeroCoordFP = yeeLayout.getZeroIncCoordFP ();

          FieldValue x = realCoord.getX () - zeroCoordFP.getX ();
          FieldValue y = realCoord.getY () - zeroCoordFP.getY ();
          FieldValue d = x * cos (incidentWaveAngle) + y * sin (incidentWaveAngle);
          FieldValue coordD1 = (FieldValue) ((grid_iter) d);
          FieldValue coordD2 = coordD1 + 1;
          FieldValue proportionD2 = d - coordD1;
          FieldValue proportionD1 = 1 - proportionD2;

          GridCoordinate1D pos1 (coordD1);
          GridCoordinate1D pos2 (coordD2);

          FieldPointValue *valE1 = EInc.getFieldPointValue (pos1);
          FieldPointValue *valE2 = EInc.getFieldPointValue (pos2);

          FieldValue diff = proportionD1 * valE1->getPrevValue () + proportionD2 * valE2->getPrevValue ();

          prevEz1 += diff;
        }
      }

      FieldValue val = calculateHy_2D_TMz (valHy->getPrevValue (),
                                           prevEz1,
                                           prevEz2,
                                           gridTimeStep,
                                           gridStep,
                                           valMu->getCurValue () * mu0);

      FieldPointValue* tmp = Hy.getFieldPointValue (pos);
      tmp->setCurValue (val);
    }
  }
}

void
SchemeTMz::calculateHyStepPML (time_step t, GridCoordinate3D HyStart, GridCoordinate3D HyEnd)
{
  FieldValue eps0 = PhysicsConst::Eps0;
  FieldValue mu0 = PhysicsConst::Mu0;

  for (int i = HyStart.getX (); i < HyEnd.getX (); ++i)
  {
    for (int j = HyStart.getY (); j < HyEnd.getY (); ++j)
    {
      GridCoordinate2D pos (i, j);

      GridCoordinate2D posAbs = Hy.getTotalPosition (pos);

      GridCoordinate2D posLeft = shrinkCoord (yeeLayout.getHyCircuitElement (pos, LayoutDirection::LEFT));
      GridCoordinate2D posRight = shrinkCoord (yeeLayout.getHyCircuitElement (pos, LayoutDirection::RIGHT));

      FieldPointValue* valBy = By.getFieldPointValue (pos);

      FieldPointValue* valSigmaZ = SigmaZ.getFieldPointValue (pos);

      FieldPointValue* valEz1 = Ez.getFieldPointValue (posRight);
      FieldPointValue* valEz2 = Ez.getFieldPointValue (posLeft);

      FieldValue prevEz1 = valEz1->getPrevValue ();
      FieldValue prevEz2 = valEz2->getPrevValue ();

      if (useTFSF)
      {
        if (yeeLayout.doNeedTFSFUpdateHyBorder (posAbs, LayoutDirection::LEFT))
        {
          GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getEzCoordFP (Ez.getTotalPosition (posLeft)));
          GridCoordinateFP2D zeroCoordFP = yeeLayout.getZeroIncCoordFP ();

          FieldValue x = realCoord.getX () - zeroCoordFP.getX ();
          FieldValue y = realCoord.getY () - zeroCoordFP.getY ();
          FieldValue d = x * cos (incidentWaveAngle) + y * sin (incidentWaveAngle);
          FieldValue coordD1 = (FieldValue) ((grid_iter) d);
          FieldValue coordD2 = coordD1 + 1;
          FieldValue proportionD2 = d - coordD1;
          FieldValue proportionD1 = 1 - proportionD2;

          GridCoordinate1D pos1 (coordD1);
          GridCoordinate1D pos2 (coordD2);

          FieldPointValue *valE1 = EInc.getFieldPointValue (pos1);
          FieldPointValue *valE2 = EInc.getFieldPointValue (pos2);

          FieldValue diff = proportionD1 * valE1->getPrevValue () + proportionD2 * valE2->getPrevValue ();

          prevEz2 += diff;
        }
        else if (yeeLayout.doNeedTFSFUpdateHyBorder (posAbs, LayoutDirection::RIGHT))
        {
          GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getEzCoordFP (Ez.getTotalPosition (posRight)));
          GridCoordinateFP2D zeroCoordFP = yeeLayout.getZeroIncCoordFP ();

          FieldValue x = realCoord.getX () - zeroCoordFP.getX ();
          FieldValue y = realCoord.getY () - zeroCoordFP.getY ();
          FieldValue d = x * cos (incidentWaveAngle) + y * sin (incidentWaveAngle);
          FieldValue coordD1 = (FieldValue) ((grid_iter) d);
          FieldValue coordD2 = coordD1 + 1;
          FieldValue proportionD2 = d - coordD1;
          FieldValue proportionD1 = 1 - proportionD2;

          GridCoordinate1D pos1 (coordD1);
          GridCoordinate1D pos2 (coordD2);

          FieldPointValue *valE1 = EInc.getFieldPointValue (pos1);
          FieldPointValue *valE2 = EInc.getFieldPointValue (pos2);

          FieldValue diff = proportionD1 * valE1->getPrevValue () + proportionD2 * valE2->getPrevValue ();

          prevEz1 += diff;
        }
      }

      FieldValue k_z = 1;

      FieldValue Ca = (2 * eps0 * k_z - valSigmaZ->getCurValue () * gridTimeStep) / (2 * eps0 * k_z + valSigmaZ->getCurValue () * gridTimeStep);
      FieldValue Cb = (2 * eps0 * gridTimeStep / gridStep) / (2 * eps0 * k_z + valSigmaZ->getCurValue () * gridTimeStep);

      FieldValue val = calculateHy_2D_TMz_Precalc (valBy->getPrevValue (),
                                                   prevEz1,
                                                   prevEz2,
                                                   Ca,
                                                   Cb);

      valBy->setCurValue (val);
    }
  }

  for (int i = HyStart.getX (); i < HyEnd.getX (); ++i)
  {
    for (int j = HyStart.getY (); j < HyEnd.getY (); ++j)
    {
      GridCoordinate2D pos (i, j);

      FieldPointValue* valHy = Hy.getFieldPointValue (pos);

      FieldPointValue* valBy = By.getFieldPointValue (pos);

      FieldPointValue* valSigmaX = SigmaX.getFieldPointValue (pos);
      FieldPointValue* valSigmaY = SigmaY.getFieldPointValue (pos);

      FieldPointValue* valMu = Mu.getFieldPointValue (pos);

      FieldValue k_x = 1;
      FieldValue k_y = 1;

      FieldValue Ca = (2 * eps0 * k_x - valSigmaX->getCurValue () * gridTimeStep) / (2 * eps0 * k_x + valSigmaX->getCurValue () * gridTimeStep);
      FieldValue Cb = ((2 * eps0 * k_y + valSigmaY->getCurValue () * gridTimeStep) / (valMu->getCurValue () * mu0)) / (2 * eps0 * k_x + valSigmaX->getCurValue () * gridTimeStep);
      FieldValue Cc = ((2 * eps0 * k_y - valSigmaY->getCurValue () * gridTimeStep) / (valMu->getCurValue () * mu0)) / (2 * eps0 * k_x + valSigmaX->getCurValue () * gridTimeStep);

      FieldValue val = calculateHy_from_By_Precalc (valHy->getPrevValue (),
                                                    valBy->getCurValue (),
                                                    valBy->getPrevValue (),
                                                    Ca,
                                                    Cb,
                                                    Cc);

      valHy->setCurValue (val);
    }
  }
}

void
SchemeTMz::performNSteps (time_step startStep, time_step numberTimeSteps, int dumpRes)
{
  GridCoordinate2D EzSize = Ez.getSize ();

  time_step stepLimit = startStep + numberTimeSteps;

  for (int t = startStep; t < stepLimit; ++t)
  {
    GridCoordinate3D EzStart = yeeLayout.getEzStart (Ez.getStart ());
    GridCoordinate3D EzEnd = yeeLayout.getEzEnd (Ez.getEnd ());

    GridCoordinate3D HxStart = yeeLayout.getHxStart (Hx.getStart ());
    GridCoordinate3D HxEnd = yeeLayout.getHxEnd (Hx.getEnd ());

    GridCoordinate3D HyStart = yeeLayout.getHyStart (Hy.getStart ());
    GridCoordinate3D HyEnd = yeeLayout.getHyEnd (Hy.getEnd ());

    if (useTFSF)
    {
      performPlaneWaveESteps (t);
    }

    performEzSteps (t, EzStart, EzEnd);

    if (!useTFSF)
    {
#if defined (PARALLEL_GRID)
      if (process == 0)
#endif
      {
        FieldValue freq = PhysicsConst::SpeedOfLight / waveLength;

        GridCoordinate2D pos (30, EzSize.getY () / 2);
        FieldPointValue* tmp = Ez.getFieldPointValue (pos);
        tmp->setCurValue (sin (gridTimeStep * t * 2 * PhysicsConst::Pi * freq));

        // for (int i = EzStart.getX (); i < EzEnd.getX (); ++i)
        // {
        //   for (int j = EzStart.getY (); j < EzEnd.getY (); ++j)
        //   {
        //     GridCoordinate2D pos (i, j);
        //
        //     GridCoordinate2D posAbs = Ez.getTotalPosition (pos);
        //
        //     GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getEzCoordFP (posAbs));
        //
        //     if (realCoord.getX () == EzSize.getX () / 2 - 20 + 0.5
        //         || realCoord.getX () == EzSize.getX () / 2 + 20 + 0.5
        //         || realCoord.getY () == EzSize.getY () / 2 - 20 + 0.5
        //         || realCoord.getY () == EzSize.getY () / 2 + 20 + 0.5)
        //     {
        //       FieldPointValue* tmp = Ez.getFieldPointValue (pos);
        //
        //       FieldValue diff_x = (i - ((FieldValue) EzSize.getX ()) / 2);
        //       FieldValue diff_y = (j - ((FieldValue) EzSize.getY ()) / 2);
        //
        //       FieldValue sqr = diff_x * diff_x + diff_y * diff_y;
        //
        //     // if (sqr >= 100 && sqr < 400)
        //     // {
        //       FieldValue inTime = gridTimeStep * t * 2 * PhysicsConst::Pi * freq;
        //       FieldValue inSpace = 2 * PhysicsConst::Pi * sqrt (sqr) / stepWaveLength;
        //
        //       if (gridTimeStep * t * PhysicsConst::SpeedOfLight - gridStep * sqrt (sqr) >= 0)
        //       {
        //         tmp->setCurValue (sin (inTime - inSpace));
        //       }
        //     }
        //   }
        // }
      }
    }

    Ez.nextTimeStep ();

    if (usePML)
    {
      Dz.nextTimeStep ();
    }

    if (useTFSF)
    {
      performPlaneWaveHSteps (t);
    }

    performHxSteps (t, HxStart, HxEnd);
    performHySteps (t, HyStart, HyEnd);

    Hx.nextTimeStep ();
    Hy.nextTimeStep ();

    if (usePML)
    {
      Bx.nextTimeStep ();
      By.nextTimeStep ();
    }

    if (t % 100 == 0)
    {
      if (dumpRes)
      {
        BMPDumper<GridCoordinate2D> dumperEz;
        dumperEz.init (t, CURRENT, process, "2D-TMz-in-time-Ez");
        dumperEz.dumpGrid (Ez);

        BMPDumper<GridCoordinate2D> dumperHx;
        dumperHx.init (t, CURRENT, process, "2D-TMz-in-time-Hx");
        dumperHx.dumpGrid (Hx);

        BMPDumper<GridCoordinate2D> dumperHy;
        dumperHy.init (t, CURRENT, process, "2D-TMz-in-time-Hy");
        dumperHy.dumpGrid (Hy);
      }
    }
  }

  if (dumpRes)
  {
    BMPDumper<GridCoordinate2D> dumperEz;
    dumperEz.init (stepLimit, CURRENT, process, "2D-TMz-in-time-Ez");
    dumperEz.dumpGrid (Ez);

    BMPDumper<GridCoordinate2D> dumperHx;
    dumperHx.init (stepLimit, CURRENT, process, "2D-TMz-in-time-Hx");
    dumperHx.dumpGrid (Hx);

    BMPDumper<GridCoordinate2D> dumperHy;
    dumperHy.init (stepLimit, CURRENT, process, "2D-TMz-in-time-Hy");
    dumperHy.dumpGrid (Hy);

    for (int i = 0; i < EzSize.getX (); ++i)
    {
      for (int j = 0; j < EzSize.getY (); ++j)
      {
        GridCoordinate2D pos (i, j);

        if (j == Ez.getSize ().getY () / 2)
        {
          FieldPointValue* tmp = Ez.getFieldPointValue (pos);

          FieldValue freq = PhysicsConst::SpeedOfLight / waveLength;
          FieldValue inTime = gridTimeStep * (stepLimit - 1) * 2 * PhysicsConst::Pi * freq;
          FieldValue inSpace = 2 * PhysicsConst::Pi * (i >= EzSize.getX ()/2 ? i - EzSize.getX ()/2 : EzSize.getX ()/2 - i) / stepWaveLength;

          printf ("%u: %f vs %f\n", i, tmp->getCurValue (), sin (inTime - inSpace));
        }
      }
    }
  }
}

void
SchemeTMz::performAmplitudeSteps (time_step startStep, int dumpRes)
{
  int is_stable_state = 0;

  GridCoordinate2D EzSize = Ez.getSize ();

  time_step t = startStep;

  GridCoordinate2D PMLSize = shrinkCoord (yeeLayout.getLeftBorderPML ());

  while (is_stable_state == 0 && t < amplitudeStepLimit)
  {
    FieldValue maxAccuracy = -1;

    //is_stable_state = 1;

    GridCoordinate3D EzStart = yeeLayout.getEzStart (Ez.getStart ());
    GridCoordinate3D EzEnd = yeeLayout.getEzEnd (Ez.getEnd ());

    GridCoordinate3D HxStart = yeeLayout.getHxStart (Hx.getStart ());
    GridCoordinate3D HxEnd = yeeLayout.getHxEnd (Hx.getEnd ());

    GridCoordinate3D HyStart = yeeLayout.getHyStart (Hy.getStart ());
    GridCoordinate3D HyEnd = yeeLayout.getHyEnd (Hy.getEnd ());

    if (useTFSF)
    {
      performPlaneWaveESteps (t);
    }

    performEzSteps (t, EzStart, EzEnd);

    if (!useTFSF)
    {
#if defined (PARALLEL_GRID)
      if (process == 0)
#endif
      {
        FieldValue freq = PhysicsConst::SpeedOfLight / waveLength;

        GridCoordinate2D pos (30, EzSize.getY () / 2);
        FieldPointValue* tmp = Ez.getFieldPointValue (pos);
        tmp->setCurValue (sin (gridTimeStep * t * 2 * PhysicsConst::Pi * freq));

        // for (int i = EzStart.getX (); i < EzEnd.getX (); ++i)
        // {
        //   for (int j = EzStart.getY (); j < EzEnd.getY (); ++j)
        //   {
        //     GridCoordinate2D pos (i, j);
        //
        //     GridCoordinate2D posAbs = Ez.getTotalPosition (pos);
        //
        //     GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getEzCoordFP (posAbs));
        //
        //     if (realCoord.getX () == EzSize.getX () / 2 - 20 + 0.5
        //         || realCoord.getX () == EzSize.getX () / 2 + 20 + 0.5
        //         || realCoord.getY () == EzSize.getY () / 2 - 20 + 0.5
        //         || realCoord.getY () == EzSize.getY () / 2 + 20 + 0.5)
        //     {
        //       FieldPointValue* tmp = Ez.getFieldPointValue (pos);
        //
        //       FieldValue diff_x = (i - ((FieldValue) EzSize.getX ()) / 2);
        //       FieldValue diff_y = (j - ((FieldValue) EzSize.getY ()) / 2);
        //
        //       FieldValue sqr = diff_x * diff_x + diff_y * diff_y;
        //
        //       FieldValue inTime = gridTimeStep * t * 2 * PhysicsConst::Pi * freq;
        //       FieldValue inSpace = 2 * PhysicsConst::Pi * sqrt (sqr) / stepWaveLength;
        //
        //     // if (sqr == 0 && )
        //     // {
        //     //   tmp->setCurValue (sin (inTime - inSpace));
        //     // }
        //
        //     // if (sqr >= 100 && sqr < 400)
        //     // {
        //
        //
        //       if (gridTimeStep * t * PhysicsConst::SpeedOfLight - gridStep * sqrt (sqr) >= 0)
        //       {
        //         tmp->setCurValue (sin (inTime - inSpace));
        //       }
        //     }
        //   }
        // }
      }
    }

    for (int i = EzStart.getX (); i < EzEnd.getX (); ++i)
    {
      for (int j = EzStart.getY (); j < EzEnd.getY (); ++j)
      {
        GridCoordinate2D pos (i, j);

        if (!yeeLayout.isEzInPML (Ez.getTotalPosition (pos)))
        {
          FieldPointValue* tmp = Ez.getFieldPointValue (pos);
          FieldPointValue* tmpAmp = EzAmplitude.getFieldPointValue (pos);

          GridCoordinateFP2D realCoord = shrinkCoord (yeeLayout.getEzCoordFP (Ez.getTotalPosition (pos)));

          GridCoordinateFP3D leftBorder = GridCoordinateFP3D (0, 0, 0) + convertCoord (yeeLayout.getLeftBorderTFSF ());
          GridCoordinateFP3D rightBorder = GridCoordinateFP3D (0, 0, 0) + convertCoord (yeeLayout.getRightBorderTFSF ());

          FieldValue val = tmp->getCurValue ();

          // if (realCoord.getX () > leftBorder.getX () - 1 && realCoord.getX () < rightBorder.getX () + 1
          //     && realCoord.getY () > leftBorder.getY () - 1 && realCoord.getY () < rightBorder.getY () + 1)
          // {
          //   GridCoordinateFP2D zeroCoordFP = yeeLayout.getZeroIncCoordFP ();
          //
          //   FieldValue x = realCoord.getX () - zeroCoordFP.getX ();
          //   FieldValue y = realCoord.getY () - zeroCoordFP.getY ();
          //   FieldValue d = x * cos (incidentWaveAngle) + y * sin (incidentWaveAngle);
          //   FieldValue coordD1 = (FieldValue) ((grid_iter) d);
          //   FieldValue coordD2 = coordD1 + 1;
          //   FieldValue proportionD2 = d - coordD1;
          //   FieldValue proportionD1 = 1 - proportionD2;
          //
          //   GridCoordinate1D pos1 (coordD1);
          //   GridCoordinate1D pos2 (coordD2);
          //
          //   FieldPointValue *valE1 = EInc.getFieldPointValue (pos1);
          //   FieldPointValue *valE2 = EInc.getFieldPointValue (pos2);
          //
          //   FieldValue diff = proportionD1 * valE1->getPrevValue () + proportionD2 * valE2->getPrevValue ();
          //
          //   val -= diff;
          // }

          if (updateAmplitude (val, tmpAmp, &maxAccuracy) == 0)
          {
            is_stable_state = 0;
          }
        }
      }
    }

    Ez.nextTimeStep ();

    if (usePML)
    {
      Dz.nextTimeStep ();
    }

    if (useTFSF)
    {
      performPlaneWaveHSteps (t);
    }

    performHxSteps (t, HxStart, HxEnd);
    performHySteps (t, HyStart, HyEnd);

    for (int i = HxStart.getX (); i < HxEnd.getX (); ++i)
    {
      for (int j = HxStart.getY (); j < HxEnd.getY (); ++j)
      {
        GridCoordinate2D pos (i, j);

        if (!yeeLayout.isHxInPML (Hx.getTotalPosition (pos)))
        {
          FieldPointValue* tmp = Hx.getFieldPointValue (pos);
          FieldPointValue* tmpAmp = HxAmplitude.getFieldPointValue (pos);

          if (updateAmplitude (tmp->getCurValue (), tmpAmp, &maxAccuracy) == 0)
          {
            is_stable_state = 0;
          }
        }
      }
    }

    for (int i = HyStart.getX (); i < HyEnd.getX (); ++i)
    {
      for (int j = HyStart.getY (); j < HyEnd.getY (); ++j)
      {
        GridCoordinate2D pos (i, j);

        if (!yeeLayout.isHyInPML (Hy.getTotalPosition (pos)))
        {

          FieldPointValue* tmp = Hy.getFieldPointValue (pos);
          FieldPointValue* tmpAmp = HyAmplitude.getFieldPointValue (pos);

          if (updateAmplitude (tmp->getCurValue (), tmpAmp, &maxAccuracy) == 0)
          {
            is_stable_state = 0;
          }
        }
      }
    }

    Hx.nextTimeStep ();
    Hy.nextTimeStep ();

    if (usePML)
    {
      Bx.nextTimeStep ();
      By.nextTimeStep ();
    }

    ++t;

    if (maxAccuracy < 0)
    {
      is_stable_state = 0;
    }

#if defined (PARALLEL_GRID)
    for (int rank = 0; rank < Ez.getTotalProcCount (); ++rank)
    {
      if (process == rank)
      {
        for (int rankDest = 0; rankDest < Ez.getTotalProcCount (); ++rankDest)
        {
          if (rankDest != rank)
          {
            int retCode = MPI_Send (&is_stable_state, 1, MPI_INT, 0, process, MPI_COMM_WORLD);

            ASSERT (retCode == MPI_SUCCESS);
          }
        }
      }
      else
      {
        MPI_Status status;

        int is_other_stable_state = 0;

        int retCode = MPI_Recv (&is_other_stable_state, 1, MPI_INT, rank, rank, MPI_COMM_WORLD, &status);

        ASSERT (retCode == MPI_SUCCESS);

        if (!is_other_stable_state)
        {
          is_stable_state = 0;
        }
      }

      MPI_Barrier (MPI_COMM_WORLD);
    }
#endif

#if PRINT_MESSAGE
    printf ("%d amplitude calculation step: max accuracy %f. \n", t, maxAccuracy);
#endif /* PRINT_MESSAGE */
  }

  if (dumpRes)
  {
    BMPDumper<GridCoordinate2D> dumper;
    dumper.init (t, CURRENT, process, "2D-TMz-amplitude-Ez");
    dumper.dumpGrid (EzAmplitude);

    FieldValue norm = 0;

    for (int i = 0; i < EzSize.getX (); ++i)
    {
      for (int j = 0; j < EzSize.getY (); ++j)
      {
        GridCoordinate2D pos (i, j);

        if (j == EzAmplitude.getSize ().getY () / 2)
        {
          FieldPointValue* tmp = EzAmplitude.getFieldPointValue (pos);

          FieldValue val1 = j0 (2 * PhysicsConst::Pi * (i >= EzSize.getX ()/2 ? i - EzSize.getX ()/2 : EzSize.getX ()/2 - i) / stepWaveLength);
          FieldValue val2 = y0 (2 * PhysicsConst::Pi * (i >= EzSize.getX ()/2 ? i - EzSize.getX ()/2 : EzSize.getX ()/2 - i) / stepWaveLength);
          FieldValue hankel =  sqrt (val1 * val1 + val2 * val2) / 4;

          if (i > EzSize.getX ()/2 && i < yeeLayout.getRightBorderPML ().getX () - 5)
          {
            norm += tmp->getCurValue () * tmp->getCurValue ();
          }

          printf ("%u: %f, %f, %f\n", i, tmp->getCurValue (), hankel, tmp->getCurValue () - hankel);
        }
      }
    }

    norm = sqrt (norm);

    printf ("Norm (C): %f\n", norm);

    for (int i = 0; i < EzSize.getX (); ++i)
    {
      for (int j = 0; j < EzSize.getY (); ++j)
      {
        GridCoordinate2D pos (i, j);

        if (j == EzAmplitude.getSize ().getY () / 2)
        {
          FieldPointValue* tmp = EzAmplitude.getFieldPointValue (pos);

          printf ("%u: %f\n", i, tmp->getCurValue () / norm);
        }
      }
    }
  }

  if (is_stable_state == 0)
  {
    ASSERT_MESSAGE ("Stable state is not reached. Increase number of steps.\n");
  }
}

int
SchemeTMz::updateAmplitude (FieldValue val, FieldPointValue *amplitudeValue, FieldValue *maxAccuracy)
{
  int is_stable_state = 1;

  FieldValue valAmp = amplitudeValue->getCurValue ();

  val = val >= 0 ? val : -val;

  if (val >= valAmp)
  {
    FieldValue accuracy = val - valAmp;
    if (valAmp != 0)
    {
      accuracy /= valAmp;
    }
    else if (val != 0)
    {
      accuracy /= val;
    }

    if (accuracy > PhysicsConst::accuracy)
    {
      is_stable_state = 0;

      amplitudeValue->setCurValue (val);
    }

    if (accuracy > *maxAccuracy)
    {
      *maxAccuracy = accuracy;
    }
  }

  return is_stable_state;
}

void
SchemeTMz::performSteps (int dumpRes)
{
#if defined (CUDA_ENABLED)

  if (usePML || useTFSF || calculateAmplitude)
  {
    ASSERT_MESSAGE ("Cuda GPU calculations with these parameters are not implemented");
  }

  CudaExitStatus status;

  cudaExecute2DTMzSteps (&status, yeeLayout, gridTimeStep, gridStep, Ez, Hx, Hy, Eps, Mu, totalStep, process);

  ASSERT (status == CUDA_OK);

  if (dumpRes)
  {
    BMPDumper<GridCoordinate2D> dumper;
    dumper.init (totalStep, ALL, process, "2D-TMz-in-time");
    dumper.dumpGrid (Ez);
  }
#else /* CUDA_ENABLED */

  performNSteps (0, totalStep, dumpRes);

  if (calculateAmplitude)
  {
    performAmplitudeSteps (totalStep, dumpRes);
  }

#endif /* !CUDA_ENABLED */
}

void
SchemeTMz::initScheme (FieldValue wLength, FieldValue step)
{
  waveLength = wLength;
  stepWaveLength = step;
  frequency = PhysicsConst::SpeedOfLight / waveLength;

  gridStep = waveLength / stepWaveLength;
  gridTimeStep = gridStep / (2 * PhysicsConst::SpeedOfLight);
}

#if defined (PARALLEL_GRID)
void
SchemeTMz::initProcess (int rank)
{
  process = rank;
}
#endif

void
SchemeTMz::initGrids ()
{
  for (int i = 0; i < Eps.getSize ().getX (); ++i)
  {
    for (int j = 0; j < Eps.getSize ().getY (); ++j)
    {
      FieldPointValue* valEps;

#if defined (TWO_TIME_STEPS)
      valEps = new FieldPointValue (1, 1, 1);
#elif defined (ONE_TIME_STEP)
      valEps = new FieldPointValue (1, 1);
#else
      valEps = new FieldPointValue (1);
#endif

      GridCoordinate2D pos (i, j);
      GridCoordinateFP2D posAbs = shrinkCoord (yeeLayout.getEpsCoordFP (Eps.getTotalPosition (pos)));

      GridCoordinateFP2D size = shrinkCoord (yeeLayout.getEpsCoordFP (Eps.getTotalSize ()));

      // if (posAbs.getX () > size.getX () / 2 && posAbs.getX () < yeeLayout.rightBorderTotalField.getX () - 10
      //     && posAbs.getY () > yeeLayout.leftBorderTotalField.getY () + 10 && posAbs.getY () < yeeLayout.rightBorderTotalField.getY () - 10)
      // {
      //   valEps->setCurValue (4.0);
      // }
      // if ((posAbs.getX () - size.getX () / 2) * (posAbs.getX () - size.getX () / 2) + (posAbs.getY () - size.getY () / 2) * (posAbs.getY () - size.getY () / 2) < (size.getX ()*1.5/7) * (size.getX ()*1.5/7))
      // {
      //   valEps->setCurValue (4.0);
      // }

      Eps.setFieldPointValue (valEps, pos);
    }
  }

  BMPDumper<GridCoordinate2D> dumper;
  dumper.init (0, CURRENT, process, "Eps");
  dumper.dumpGrid (Eps);

  for (int i = 0; i < Mu.getSize ().getX (); ++i)
  {
    for (int j = 0; j < Mu.getSize ().getY (); ++j)
    {
      FieldPointValue* valMu;

#if defined (TWO_TIME_STEPS)
      valMu = new FieldPointValue (1, 1, 1);
#elif defined (ONE_TIME_STEP)
      valMu = new FieldPointValue (1, 1);
#else
      valMu = new FieldPointValue (1);
#endif

      GridCoordinate2D pos (i, j);

      Mu.setFieldPointValue (valMu, pos);
    }
  }

  FieldValue eps0 = PhysicsConst::Eps0;
  FieldValue mu0 = PhysicsConst::Mu0;

  GridCoordinate2D PMLSize = shrinkCoord (yeeLayout.getLeftBorderPML ());

  FieldValue boundary = PMLSize.getX () * gridStep;
  uint32_t exponent = 6;
	FieldValue R_err = 1e-16;
	FieldValue sigma_max_1 = -log (R_err) * (exponent + 1.0) / (2.0 * sqrt (mu0 / eps0) * boundary);
	FieldValue boundaryFactor = sigma_max_1 / (gridStep * (pow (boundary, exponent)) * (exponent + 1));

  for (int i = 0; i < SigmaX.getSize ().getX (); ++i)
  {
    for (int j = 0; j < SigmaX.getSize ().getY (); ++j)
    {
      FieldPointValue* valSigma;

#if defined (TWO_TIME_STEPS)
      valSigma = new FieldPointValue (0, 0, 0);
#elif defined (ONE_TIME_STEP)
      valSigma = new FieldPointValue (0, 0);
#else
      valSigma = new FieldPointValue (0);
#endif

      GridCoordinate2D pos (i, j);
      GridCoordinateFP2D posAbs = shrinkCoord (yeeLayout.getEpsCoordFP (SigmaX.getTotalPosition (pos)));

      GridCoordinateFP2D size = shrinkCoord (yeeLayout.getEpsCoordFP (SigmaX.getTotalSize ()));

      /*
       * FIXME: add layout coordinates for material: sigma, eps, etc.
       */
      if (posAbs.getX () < PMLSize.getX ())
      {
        grid_coord dist = PMLSize.getX () - posAbs.getX ();
  			FieldValue x1 = (dist + 1) * gridStep;       // upper bounds for point i
  			FieldValue x2 = dist * gridStep;       // lower bounds for point i

  			valSigma->setCurValue (boundaryFactor * (pow (x1, (exponent + 1)) - pow (x2, (exponent + 1))));   //   polynomial grading
      }
      else if (posAbs.getX () >= size.getX () - PMLSize.getX ())
      {
        grid_coord dist = posAbs.getX () - (size.getX () - PMLSize.getX ());
  			FieldValue x1 = (dist + 1) * gridStep;       // upper bounds for point i
  			FieldValue x2 = dist * gridStep;       // lower bounds for point i

  			//std::cout << boundaryFactor * (pow(x1, (exponent + 1)) - pow(x2, (exponent + 1))) << std::endl;
  			valSigma->setCurValue (boundaryFactor * (pow (x1, (exponent + 1)) - pow (x2, (exponent + 1))));   //   polynomial grading
      }

      SigmaX.setFieldPointValue (valSigma, pos);
    }
  }

  for (int i = 0; i < SigmaY.getSize ().getX (); ++i)
  {
    for (int j = 0; j < SigmaY.getSize ().getY (); ++j)
    {
      FieldPointValue* valSigma;

#if defined (TWO_TIME_STEPS)
      valSigma = new FieldPointValue (0, 0, 0);
#elif defined (ONE_TIME_STEP)
      valSigma = new FieldPointValue (0, 0);
#else
      valSigma = new FieldPointValue (0);
#endif

      GridCoordinate2D pos (i, j);
      GridCoordinateFP2D posAbs = shrinkCoord (yeeLayout.getEpsCoordFP (SigmaY.getTotalPosition (pos)));

      GridCoordinateFP2D size = shrinkCoord (yeeLayout.getEpsCoordFP (SigmaX.getTotalSize ()));

      /*
       * FIXME: add layout coordinates for material: sigma, eps, etc.
       */
      if (posAbs.getY () < PMLSize.getY ())
      {
        grid_coord dist = PMLSize.getY () - posAbs.getY ();
        FieldValue x1 = (dist + 1) * gridStep;       // upper bounds for point i
        FieldValue x2 = dist * gridStep;       // lower bounds for point i

        valSigma->setCurValue (boundaryFactor * (pow (x1, (exponent + 1)) - pow (x2, (exponent + 1))));   //   polynomial grading
      }
      else if (posAbs.getY () >= size.getY () - PMLSize.getY ())
      {
        grid_coord dist = posAbs.getY () - (size.getY () - PMLSize.getY ());
        FieldValue x1 = (dist + 1) * gridStep;       // upper bounds for point i
        FieldValue x2 = dist * gridStep;       // lower bounds for point i

        //std::cout << boundaryFactor * (pow(x1, (exponent + 1)) - pow(x2, (exponent + 1))) << std::endl;
        valSigma->setCurValue (boundaryFactor * (pow (x1, (exponent + 1)) - pow (x2, (exponent + 1))));   //   polynomial grading
      }

      SigmaY.setFieldPointValue (valSigma, pos);
    }
  }

  /*
   * FIXME: SigmaZ grid could be replaced with constant 0.0
   */
  for (int i = 0; i < SigmaZ.getSize ().getX (); ++i)
  {
    for (int j = 0; j < SigmaZ.getSize ().getY (); ++j)
    {
      FieldPointValue* valSigma;

#if defined (TWO_TIME_STEPS)
      valSigma = new FieldPointValue (0, 0, 0);
#elif defined (ONE_TIME_STEP)
      valSigma = new FieldPointValue (0, 0);
#else
      valSigma = new FieldPointValue (0);
#endif

      GridCoordinate2D pos (i, j);

      SigmaZ.setFieldPointValue (valSigma, pos);
    }
  }

  {
    BMPDumper<GridCoordinate2D> dumper;
    dumper.init (0, CURRENT, process, "SigmaX");
    dumper.dumpGrid (SigmaX);
  }

  {
    BMPDumper<GridCoordinate2D> dumper;
    dumper.init (0, CURRENT, process, "SigmaY");
    dumper.dumpGrid (SigmaY);
  }

  {
    BMPDumper<GridCoordinate2D> dumper;
    dumper.init (0, CURRENT, process, "SigmaZ");
    dumper.dumpGrid (SigmaZ);
  }

  for (int i = 0; i < Ez.getSize ().getX (); ++i)
  {
    for (int j = 0; j < Ez.getSize ().getY (); ++j)
    {
      FieldPointValue* valEzAmp;
      FieldPointValue* valEz;
      FieldPointValue* valDz;

#if defined (TWO_TIME_STEPS)
      valEz = new FieldPointValue (0, 0, 0);

      valDz = new FieldPointValue (0, 0, 0);

      if (calculateAmplitude)
      {
        valEzAmp = new FieldPointValue (0, 0, 0);
      }
#elif defined (ONE_TIME_STEP)
      valEz = new FieldPointValue (0, 0);

      valDz = new FieldPointValue (0, 0);

      if (calculateAmplitude)
      {
        valEzAmp = new FieldPointValue (0, 0);
      }
#else
      valEz = new FieldPointValue (0);

      valDz = new FieldPointValue (0);

      if (calculateAmplitude)
      {
        valEzAmp = new FieldPointValue (0);
      }
#endif

      GridCoordinate2D pos (i, j);

      Ez.setFieldPointValue (valEz, pos);

      Dz.setFieldPointValue (valDz, pos);

      if (calculateAmplitude)
      {
        EzAmplitude.setFieldPointValue (valEzAmp, pos);
      }
    }
  }

  for (int i = 0; i < Hx.getSize ().getX (); ++i)
  {
    for (int j = 0; j < Hx.getSize ().getY (); ++j)
    {
      FieldPointValue* valHxAmp;
      FieldPointValue* valHx;

      FieldPointValue* valBx;

#if defined (TWO_TIME_STEPS)
      valHx = new FieldPointValue (0, 0, 0);

      valBx = new FieldPointValue (0, 0, 0);

      if (calculateAmplitude)
      {
        valHxAmp = new FieldPointValue (0, 0, 0);
      }
#elif defined (ONE_TIME_STEP)
      valHx = new FieldPointValue (0, 0);

      valBx = new FieldPointValue (0, 0);

      if (calculateAmplitude)
      {
        valHxAmp = new FieldPointValue (0, 0);
      }
#else
      valHx = new FieldPointValue (0);

      valBx = new FieldPointValue (0);

      if (calculateAmplitude)
      {
        valHxAmp = new FieldPointValue (0);
      }
#endif

      GridCoordinate2D pos (i, j);

      Hx.setFieldPointValue (valHx, pos);

      Bx.setFieldPointValue (valBx, pos);

      if (calculateAmplitude)
      {
        HxAmplitude.setFieldPointValue (valHxAmp, pos);
      }
    }
  }

  for (int i = 0; i < Hy.getSize ().getX (); ++i)
  {
    for (int j = 0; j < Hy.getSize ().getY (); ++j)
    {
      FieldPointValue* valHyAmp;
      FieldPointValue* valHy;

      FieldPointValue* valBy;

#if defined (TWO_TIME_STEPS)
      valHy = new FieldPointValue (0, 0, 0);

      valBy = new FieldPointValue (0, 0, 0);

      if (calculateAmplitude)
      {
        valHyAmp = new FieldPointValue (0, 0, 0);
      }
#elif defined (ONE_TIME_STEP)
      valHy = new FieldPointValue (0, 0);

      valBy = new FieldPointValue (0, 0);

      if (calculateAmplitude)
      {
        valHyAmp = new FieldPointValue (0, 0);
      }
#else
      valHy = new FieldPointValue (0);

      valBy = new FieldPointValue (0);

      if (calculateAmplitude)
      {
        valHyAmp = new FieldPointValue (0);
      }
#endif

      GridCoordinate2D pos (i, j);

      Hy.setFieldPointValue (valHy, pos);

      By.setFieldPointValue (valBy, pos);

      if (calculateAmplitude)
      {
        HyAmplitude.setFieldPointValue (valHyAmp, pos);
      }
    }
  }

  if (useTFSF)
  {
    for (grid_coord i = 0; i < EInc.getSize ().getX (); ++i)
    {
      FieldPointValue* valE;

#if defined (TWO_TIME_STEPS)
      valE = new FieldPointValue (0, 0, 0);
#elif defined (ONE_TIME_STEP)
      valE = new FieldPointValue (0, 0);
#else
      valE = new FieldPointValue (0);
#endif

      GridCoordinate1D pos (i);

      EInc.setFieldPointValue (valE, pos);
    }

    for (grid_coord i = 0; i < HInc.getSize ().getX (); ++i)
    {
      FieldPointValue* valH;

#if defined (TWO_TIME_STEPS)
      valH = new FieldPointValue (0, 0, 0);
#elif defined (ONE_TIME_STEP)
      valH = new FieldPointValue (0, 0);
#else
      valH = new FieldPointValue (0);
#endif

      GridCoordinate1D pos (i);

      HInc.setFieldPointValue (valH, pos);
    }
  }

#if defined (PARALLEL_GRID)
  MPI_Barrier (MPI_COMM_WORLD);
#endif

#if defined (PARALLEL_GRID)
  Eps.share ();
  Mu.share ();
#endif
}

#endif /* GRID_2D */
