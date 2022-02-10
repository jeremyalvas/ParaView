/*=========================================================================

Program:   Visualization Toolkit
Module:    vtkCGNSWriter.h

Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
All rights reserved.
See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

This software is distributed WITHOUT ANY WARRANTY; without even
the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/*----------------------------------------------------------------------------
Copyright (c) Maritime Research Institute Netherlands (MARIN)
See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.
----------------------------------------------------------------------------*/

#include "vtkCGNSWriter.h"

#include "vtkAppendDataSets.h"
#include "vtkArrayIteratorIncludes.h"
#include "vtkCellArray.h"
#include "vtkCellData.h"
#include "vtkCellTypes.h"
#include "vtkCompositeDataIterator.h"
#include "vtkCompositeDataSet.h"
#include "vtkDataObject.h"
#include "vtkDataObjectTreeIterator.h"
#include "vtkDoubleArray.h"
#include "vtkFieldData.h"
#include "vtkFloatArray.h"
#include "vtkIdList.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkIntArray.h"
#include "vtkMultiBlockDataSet.h"
#include "vtkMultiPieceDataSet.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkPolyData.h"
#include "vtkStdString.h"
#include "vtkStreamingDemandDrivenPipeline.h"
#include "vtkStringArray.h"
#include "vtkStructuredGrid.h"
#include "vtkThreshold.h"
#include "vtkUnstructuredGrid.h"

// clang-format off
#include "vtk_cgns.h"
#include VTK_CGNS(cgnslib.h)
// clang-format on

#include <map>
#include <set>
#include <sstream>
#include <vector>

using namespace std;

// macro to check a CGNS operation that can return CG_OK or CG_ERROR
// the macro will set the 'error' (string) variable to the CGNS error
// and return false.
#define cg_check_operation(op)                                                                     \
  if (CG_OK != (op))                                                                               \
  {                                                                                                \
    error = string(__FUNCTION__) + ":" + to_string(__LINE__) + "> " + cg_get_error();              \
    return false;                                                                                  \
  }

// CGNS starts counting at 1
#define CGNS_COUNTING_OFFSET 1

struct write_info
{
  int F, B, Z, Sol;
  int CellDim;
  bool WritePolygonalZone;
  bool WriteAllTimeSteps;
  double TimeStep;
  const char* FileName;
  const char* BaseName;
  const char* ZoneName;
  const char* SolutionName;

  map<string, int> SolutionNames;

  write_info()
  {
    F = B = Z = Sol = 0;
    CellDim = 3;
    WritePolygonalZone = false;
    WriteAllTimeSteps = false;
    TimeStep = 0.0;

    FileName = nullptr;
    BaseName = nullptr;
    ZoneName = nullptr;
    SolutionName = nullptr;
  }
};

class vtkCGNSWriter::vtkPrivate
{
public:
  // open and initialize a CGNS file
  static bool InitCGNSFile(write_info& info, const char* filename, string& error);
  static bool WriteBase(write_info& info, const char* basename, string& error);

  // write a single data set to file
  static bool WriteStructuredGrid(
    vtkStructuredGrid* structuredGrid, write_info& info, string& error);
  static bool WritePointSet(vtkPointSet* grid, write_info& info, string& error);

  // write a composite dataset to file
  static bool WriteComposite(vtkCompositeDataSet* composite, write_info& info, string& error);

  static void Flatten(vtkCompositeDataSet* composite,
    vector<vtkSmartPointer<vtkDataObject>>& objects2D,
    vector<vtkSmartPointer<vtkDataObject>>& objects3d, int zoneOffset, string& name);
  static int DetermineCellDimension(vtkPointSet* pointSet);
  static void SetNameToLength(string& name, const vector<vtkSmartPointer<vtkDataObject>>& objects);
  static bool WriteGridsToBase(vector<vtkSmartPointer<vtkDataObject>> blocks,
    vector<vtkSmartPointer<vtkDataObject>> otherBlocks, write_info& info, string& error);

protected:
  // open and initialize a CGNS file
  static bool InitCGNSFile(write_info& info, string& error);
  static bool WriteBase(write_info& info, string& error);

  static bool WriteComposite(write_info& info, vtkCompositeDataSet*, string& error);
  static bool WritePoints(write_info& info, vtkPoints* pts, string& error);

  static bool WriteFieldArray(write_info& info, CGNS_ENUMT(GridLocation_t) location,
    vtkDataSetAttributes* dsa, map<unsigned char, vector<vtkIdType>>* cellTypeMap, string& error);

  static bool WriteStructuredGrid(
    write_info& info, vtkStructuredGrid* structuredGrid, string& error);
  static bool WritePointSet(write_info& info, vtkPointSet* grid, string& error);
  static bool WritePolygonalZone(write_info& info, vtkPointSet* grid, string& error);
  static bool WriteCells(write_info& info, vtkPointSet* grid,
    map<unsigned char, vector<vtkIdType>>& cellTypeMap, string& error);

  static bool WriteBaseTimeInformation(write_info& info, string& error);
  static bool WriteZoneTimeInformation(write_info& info, string& error);
};

bool vtkCGNSWriter::vtkPrivate::WriteCells(write_info& info, vtkPointSet* grid,
  map<unsigned char, vector<vtkIdType>>& cellTypeMap, string& error)
{
  if (!grid)
  {
    error = "Grid pointer not valid.";
    return false;
  }

  if (info.WritePolygonalZone)
  {
    return WritePolygonalZone(info, grid, error);
  }

  for (vtkIdType i = 0; i < grid->GetNumberOfCells(); ++i)
  {
    unsigned char cellType = grid->GetCellType(i);
    cellTypeMap[cellType].push_back(i);
  }

  cgsize_t nCellsWritten(CGNS_COUNTING_OFFSET);
  for (auto& entry : cellTypeMap)
  {
    const unsigned char cellType = entry.first;
    CGNS_ENUMT(ElementType_t) cg_elem(CGNS_ENUMV(ElementTypeNull));
    const char* sectionname(nullptr);
    switch (cellType)
    {
      case VTK_TRIANGLE:
        cg_elem = CGNS_ENUMV(TRI_3);
        sectionname = "Elem_Triangles";
        break;
      case VTK_QUAD:
        cg_elem = CGNS_ENUMV(QUAD_4);
        sectionname = "Elem_Quads";
        break;
      case VTK_PYRAMID:
        cg_elem = CGNS_ENUMV(PYRA_5);
        sectionname = "Elem_Pyramids";
        break;
      case VTK_WEDGE:
        cg_elem = CGNS_ENUMV(PENTA_6);
        sectionname = "Elem_Wedges";
        break;
      case VTK_TETRA:
        cg_elem = CGNS_ENUMV(TETRA_4);
        sectionname = "Elem_Tetras";
        break;
      case VTK_HEXAHEDRON:
        cg_elem = CGNS_ENUMV(HEXA_8);
        sectionname = "Elem_Hexas";
        break;
      default:
        // report error?
        continue;
    }

    const vector<vtkIdType>& cellIdsOfType = entry.second;
    vector<cgsize_t> cellsOfTypeArray;

    for (auto& cellId : cellIdsOfType)
    {
      vtkCell* cell = grid->GetCell(cellId);
      const int nIds = cell->GetNumberOfPoints();

      for (int j = 0; j < nIds; ++j)
      {
        cellsOfTypeArray.push_back(
          static_cast<cgsize_t>(cell->GetPointId(j) + CGNS_COUNTING_OFFSET));
      }
    }

    int dummy(0);
    const cgsize_t start(nCellsWritten);
    const cgsize_t end(static_cast<cgsize_t>(nCellsWritten + cellIdsOfType.size() - 1));
    const int nBoundary(0);
    cg_check_operation(cg_section_write(info.F, info.B, info.Z, sectionname, cg_elem, start, end,
      nBoundary, cellsOfTypeArray.data(), &dummy));

    nCellsWritten += cellIdsOfType.size();
  }

  return true;
}

bool vtkCGNSWriter::vtkPrivate::WritePolygonalZone(
  write_info& info, vtkPointSet* grid, string& error)
{
  if (!grid)
  {
    error = "Grid pointer not valid.";
    return false;
  }

  // write all cells in the grid as polyhedra. One polyhedron consists of
  // multiple faces. The faces are written to the NGON_n Element_t array
  // and each cell references a face. Note that this does not have the
  // concept of shared faces!

  // if a cell is not a volume cell, write it as an NGON_n entry only.

  vector<cgsize_t> cellDataArr;
  vector<cgsize_t> faceDataArr;
  vector<cgsize_t> cellDataIdx;
  vector<cgsize_t> faceDataIdx;
  cellDataIdx.push_back(0);
  faceDataIdx.push_back(0);

  cgsize_t ngons(0), ncells(0);
  for (vtkIdType i = 0; i < grid->GetNumberOfCells(); ++i)
  {
    vtkCell* cell = grid->GetCell(i);
    int nFaces = cell->GetNumberOfFaces();
    if (nFaces == 0) // this is a tri, quad or polygon and has no faces. Yes it has 1 face, but 0
                     // are reported.
    {
      cgsize_t nPts = static_cast<cgsize_t>(cell->GetNumberOfPoints());
#if CGNS_VERSION >= 3400
      faceDataIdx.push_back(nPts);
#else
      faceDataArr.push_back(nPts);
#endif
      for (int p = 0; p < nPts; ++p)
      {
        faceDataArr.push_back(static_cast<cgsize_t>(CGNS_COUNTING_OFFSET + cell->GetPointId(p)));
      }
      ++ngons;
      continue;
    }

#if CGNS_VERSION >= 3400
    cellDataIdx.push_back(nFaces);
#else
    cellDataArr.push_back(nFaces);
#endif
    for (int f = 0; f < nFaces; ++f)
    {
      vtkCell* face = cell->GetFace(f);
      cgsize_t nPts = static_cast<cgsize_t>(face->GetNumberOfPoints());

      // NFACE_n references an ngon in the NGON_n array
      cellDataArr.push_back(CGNS_COUNTING_OFFSET + ngons); // ngons start counting at 0
#if CGNS_VERSION >= 3400
      faceDataIdx.push_back(nPts);
#else
      faceDataArr.push_back(nPts);
#endif
      for (int p = 0; p < nPts; ++p)
      {
        faceDataArr.push_back(static_cast<cgsize_t>(CGNS_COUNTING_OFFSET + face->GetPointId(p)));
      }
      ++ngons;
    }
    ++ncells;
  }

#if CGNS_VERSION >= 3400
  // update offsets for faces and cells
  for (size_t idx = 1; idx < faceDataIdx.size(); ++idx)
  {
    faceDataIdx[idx] += faceDataIdx[idx - 1];
  }
  for (size_t idx = 1; idx < cellDataIdx.size(); ++idx)
  {
    cellDataIdx[idx] += cellDataIdx[idx - 1];
  }
#endif

  int dummy(0);
  int nBoundary(0);
  if (ncells > 0)
  {
#if CGNS_VERSION >= 3400
    cg_check_operation(
      cg_poly_section_write(info.F, info.B, info.Z, "Elem_NFACE_n", CGNS_ENUMV(NFACE_n), 1 + ngons,
        ncells + ngons, nBoundary, cellDataArr.data(), cellDataIdx.data(), &dummy));
#else
    cg_check_operation(cg_section_write(info.F, info.B, info.Z, "Elem_NFACE_n", CGNS_ENUMV(NFACE_n),
      1 + ngons, ncells + ngons, nBoundary, cellDataArr.data(), &dummy));
#endif
  }

  if (ngons > 0)
  {
#if CGNS_VERSION >= 3400
    cg_check_operation(cg_poly_section_write(info.F, info.B, info.Z, "Elem_NGON_n",
      CGNS_ENUMV(NGON_n), 1, ngons, nBoundary, faceDataArr.data(), faceDataIdx.data(), &dummy));
#else
    cg_check_operation(cg_section_write(info.F, info.B, info.Z, "Elem_NGON_n", CGNS_ENUMV(NGON_n),
      1, ngons, nBoundary, faceDataArr.data(), &dummy));
#endif
  }

  return true;
}

bool vtkCGNSWriter::vtkPrivate::WritePointSet(write_info& info, vtkPointSet* grid, string& error)
{
  const cgsize_t nPts = static_cast<cgsize_t>(grid->GetNumberOfPoints());
  const cgsize_t nCells = static_cast<cgsize_t>(grid->GetNumberOfCells());
  if (nPts == 0 && nCells == 0)
  {
    // don't write anything
    return true;
  }
  cgsize_t dim[3] = { nPts, nCells, 0 };

  bool isPolygonal(false);
  for (vtkIdType i = 0; i < grid->GetNumberOfCells(); ++i)
  {
    vtkCell* cell = grid->GetCell(i);
    const unsigned char cellType = cell->GetCellType();

    isPolygonal |= cellType == VTK_POLYHEDRON;
    isPolygonal |= cellType == VTK_POLYGON;
  }
  info.WritePolygonalZone = isPolygonal;

  cg_check_operation(
    cg_zone_write(info.F, info.B, info.ZoneName, dim, CGNS_ENUMV(Unstructured), &(info.Z)));

  vtkPoints* pts = grid->GetPoints();

  if (!WritePoints(info, pts, error))
  {
    return false;
  }
  map<unsigned char, vector<vtkIdType>> cellTypeMap;
  if (!WriteCells(info, grid, cellTypeMap, error))
  {
    return false;
  }

  info.SolutionName = "PointData";
  if (!WriteFieldArray(info, CGNS_ENUMV(Vertex), grid->GetPointData(), nullptr, error))
  {
    return false;
  }

  info.SolutionName = "CellData";
  auto ptr = (isPolygonal ? nullptr : &cellTypeMap);
  if (!WriteFieldArray(info, CGNS_ENUMV(CellCenter), grid->GetCellData(), ptr, error))
  {
    return false;
  }

  return WriteZoneTimeInformation(info, error);
}

bool vtkCGNSWriter::vtkPrivate::WriteZoneTimeInformation(write_info& info, string& error)
{
  if (info.SolutionNames.empty())
  {
    return true;
  }

  auto at = info.SolutionNames.find("CellData");
  bool hasCellData = at != info.SolutionNames.end();
  int cellDataS = hasCellData ? at->second : -1;
  at = info.SolutionNames.find("PointData");
  bool hasVertData = at != info.SolutionNames.end();
  int vertDataS = hasVertData ? at->second : -1;

  cgsize_t dim[2] = { 32, 1 };
  if (!hasCellData && !hasVertData)
  {
    error = "No cell data or vert data found, but solution names not empty.";
    return false;
  }

  if (hasCellData || hasVertData)
  {
    cg_check_operation(cg_ziter_write(info.F, info.B, info.Z, "ZoneIterativeData_t"));
    cg_check_operation(cg_goto(info.F, info.B, "Zone_t", info.Z, "ZoneIterativeData_t", 1, "end"));

    if (hasVertData)
    {
      int sol[1] = { vertDataS };
      const char* timeStepNames = "PointData\0                      ";
      cg_check_operation(
        cg_array_write("FlowSolutionVertexPointers", CGNS_ENUMV(Character), 2, dim, timeStepNames));
      cg_check_operation(
        cg_array_write("VertexSolutionIndices", CGNS_ENUMV(Integer), 1, &dim[1], sol));
      cg_check_operation(cg_descriptor_write("VertexPrefix", "Vertex"));
    }
    if (hasCellData)
    {
      int sol[1] = { cellDataS };
      const char* timeStepNames = "CellData\0                       ";
      cg_check_operation(
        cg_array_write("FlowSolutionCellPointers", CGNS_ENUMV(Character), 2, dim, timeStepNames));
      cg_check_operation(cg_array_write("CellCenterIndices", CGNS_ENUMV(Integer), 1, &dim[1], sol));
      cg_check_operation(cg_descriptor_write("CellCenterPrefix", "CellCenter"));
    }
  }
  return true;
}

bool vtkCGNSWriter::vtkPrivate::WriteBaseTimeInformation(write_info& info, string& error)
{
  double time[1] = { info.TimeStep };

  cg_check_operation(cg_biter_write(info.F, info.B, "TimeIterValues", 1));
  cg_check_operation(cg_goto(info.F, info.B, "BaseIterativeData_t", 1, "end"));

  cgsize_t dimTimeValues[1] = { 1 };
  cg_check_operation(cg_array_write("TimeValues", CGNS_ENUMV(RealDouble), 1, dimTimeValues, time));

  cg_check_operation(cg_simulation_type_write(info.F, info.B, CGNS_ENUMV(TimeAccurate)));
  return true;
}

bool vtkCGNSWriter::vtkPrivate::WritePointSet(vtkPointSet* grid, write_info& info, string& error)
{
  if (grid->IsA("vtkPolyData"))
  {
    info.CellDim = 2;
  }
  else if (grid->IsA("vtkUnstructuredGrid"))
  {
    info.CellDim = 1;
    for (int i = 0; i < grid->GetNumberOfCells(); ++i)
    {
      vtkCell* cell = grid->GetCell(i);
      int cellDim = cell->GetCellDimension();
      if (info.CellDim < cellDim)
      {
        info.CellDim = cellDim;
      }
    }
  }

  if (!InitCGNSFile(info, error))
  {
    return false;
  }
  info.BaseName = "Base";
  if (!WriteBase(info, error))
  {
    return false;
  }

  info.ZoneName = "Zone 1";
  const bool rc = WritePointSet(info, grid, error);
  cg_check_operation(cg_close(info.F));
  return rc;
}

/*
 This function assigns the correct order of data elements for cases where
 multiple element types are written in different sections in a CGNS zone.
 Because all similar cells are grouped in the CGNS file, this means that
 the order of the cells is different in the file compared to the data arrays.
 To compensate for that, the data is reordered to follow the cell order in the file.
*/
void ReorderData(vector<double>& temp, map<unsigned char, vector<vtkIdType>>* cellTypeMap)
{
  vector<double> reordered(temp.size());

  size_t i(0);
  for (auto& entry : *cellTypeMap)
  {
    const vector<vtkIdType>& cellIdsOfType = entry.second;
    for (size_t j = 0; j < cellIdsOfType.size(); ++j)
    {
      reordered[i++] = temp[cellIdsOfType[j]];
    }
  }

  for (i = 0; i < temp.size(); ++i)
  {
    temp[i] = reordered[i];
  }
}

// writes a field array to a new solution
bool vtkCGNSWriter::vtkPrivate::WriteFieldArray(write_info& info,
  CGNS_ENUMT(GridLocation_t) location, vtkDataSetAttributes* dsa,
  map<unsigned char, vector<vtkIdType>>* cellTypeMap, string& error)
{
  if (!dsa)
  {
    error = "No valid pointer to dataset attributes.";
    return false;
  }

  const int nArr = dsa->GetNumberOfArrays();
  if (nArr > 0)
  {
    vector<double> temp;

    int dummy(0);
    cg_check_operation(
      cg_sol_write(info.F, info.B, info.Z, info.SolutionName, location, &(info.Sol)));
    info.SolutionNames.emplace(info.SolutionName, info.Sol);

    for (int i = 0; i < nArr; ++i)
    {
      vtkDataArray* da = dsa->GetArray(i);
      if (!da)
        continue;

      temp.reserve(da->GetNumberOfTuples());
      if (da->GetNumberOfComponents() != 1)
      {
        const string fieldName = da->GetName();
        if (da->GetNumberOfComponents() == 3)
        {
          // here we have to stripe the XYZ values, same as with the vertices.
          const char* const components[3] = { "X", "Y", "Z" };

          for (int idx = 0; idx < 3; ++idx)
          {
            for (vtkIdType t = 0; t < da->GetNumberOfTuples(); ++t)
            {
              double* tpl = da->GetTuple(t);
              temp.push_back(tpl[idx]);
            }

            if (location == CGNS_ENUMV(CellCenter) && cellTypeMap)
            {
              ReorderData(temp, cellTypeMap);
            }

            string fieldComponentName = fieldName + components[idx];

            cg_check_operation(cg_field_write(info.F, info.B, info.Z, info.Sol,
              CGNS_ENUMV(RealDouble), fieldComponentName.c_str(), temp.data(), &dummy));

            temp.clear();
          }
        }
        else
        {
          vtkWarningWithObjectMacro(nullptr, << " Field " << da->GetName() << " has "
                                             << da->GetNumberOfComponents()
                                             << " components, which is not supported. Skipping...");
        }
      }
      else // 1-component field data.
      {
        // force to double precision, even if data type is single precision,
        // see https://gitlab.kitware.com/paraview/paraview/-/issues/18827
        for (vtkIdType t = 0; t < da->GetNumberOfTuples(); ++t)
        {
          double* tpl = da->GetTuple(t);
          temp.push_back(*tpl);
        }

        if (location == CGNS_ENUMV(CellCenter) && cellTypeMap)
        {
          ReorderData(temp, cellTypeMap);
        }

        cg_check_operation(cg_field_write(info.F, info.B, info.Z, info.Sol, CGNS_ENUMV(RealDouble),
          da->GetName(), temp.data(), &dummy));

        temp.clear();
      }
    }
  }
  return true;
}

bool vtkCGNSWriter::vtkPrivate::WritePoints(write_info& info, vtkPoints* pts, string& error)
{
  // is there a better way to do this, other than creating temp array and striping X, Y an Z into
  // separate arrays?
  // maybe using the low-level API where I think a stride can be given while writing.
  const char* names[3] = { "CoordinateX", "CoordinateY", "CoordinateZ" };

  double* temp = new (nothrow) double[pts->GetNumberOfPoints()];
  if (!temp)
  {
    error = "Failed to allocate temporary array";
    return false;
  }

  for (int idx = 0; idx < 3; ++idx)
  {
    for (vtkIdType i = 0; i < pts->GetNumberOfPoints(); ++i)
    {
      double* xyz = pts->GetPoint(i);
      temp[i] = xyz[idx];
    }
    int dummy(0);
    if (CG_OK !=
      cg_coord_write(info.F, info.B, info.Z, CGNS_ENUMV(RealDouble), names[idx], temp, &dummy))
    {
      delete[] temp; // don't leak
      error = cg_get_error();
      return false;
    }
  }

  delete[] temp; // don't leak
  return true;
}

bool vtkCGNSWriter::vtkPrivate::InitCGNSFile(write_info& info, string& error)
{
  if (!info.FileName)
  {
    error = "File name not defined.";
    return false;
  }

  cg_check_operation(cg_open(info.FileName, CG_MODE_WRITE, &(info.F)));
  return true;
}

bool vtkCGNSWriter::vtkPrivate::WriteBase(write_info& info, string& error)
{
  if (!info.BaseName)
  {
    error = "Base name not defined.";
    return false;
  }

  cg_check_operation(cg_base_write(info.F, info.BaseName, info.CellDim, 3, &(info.B)));
  return WriteBaseTimeInformation(info, error);
}

bool vtkCGNSWriter::vtkPrivate::WriteStructuredGrid(
  write_info& info, vtkStructuredGrid* structuredGrid, string& error)
{
  if (structuredGrid->GetNumberOfCells() == 0 && structuredGrid->GetNumberOfPoints() == 0)
  {
    // don't write anything
    return true;
  }
  if (!info.ZoneName)
  {
    error = "Zone name not defined.";
    return false;
  }

  cgsize_t dim[9];

  // set the dimensions
  int* pointDims = structuredGrid->GetDimensions();
  int cellDims[3];
  int j;

  structuredGrid->GetCellDims(cellDims);

  if (!pointDims)
  {
    error = "Failed to get vertex dimensions.";
    return false;
  }

  // init dimensions
  for (int i = 0; i < 3; ++i)
  {
    dim[0 * 3 + i] = 1;
    dim[1 * 3 + i] = 0;
    dim[2 * 3 + i] = 0; // always 0 for structured
  }
  j = 0;
  for (int i = 0; i < 3; ++i)
  {
    // skip unitary index dimension
    if (pointDims[i] == 1)
    {
      continue;
    }
    dim[0 * 3 + j] = pointDims[i];
    dim[1 * 3 + j] = cellDims[i];
    j++;
  }
  // Repacking dimension in case j < 3 because CGNS expects a resized dim matrix
  // For instance if j == 2 then move from 3x3 matrix to 3x2 matrix
  for (int k = 1; (k < 3) && (j < 3); ++k)
  {
    for (int i = 0; i < j; ++i)
    {
      dim[j * k + i] = dim[3 * k + i];
    }
  }

  // create the structured zone. Cells are implicit
  cg_check_operation(
    cg_zone_write(info.F, info.B, info.ZoneName, dim, CGNS_ENUMV(Structured), &(info.Z)));

  vtkPoints* pts = structuredGrid->GetPoints();

  if (!WritePoints(info, pts, error))
  {
    return false;
  }

  info.SolutionName = "PointData";
  if (!WriteFieldArray(info, CGNS_ENUMV(Vertex), structuredGrid->GetPointData(), nullptr, error))
  {
    return false;
  }

  info.SolutionName = "CellData";
  if (!WriteFieldArray(info, CGNS_ENUMV(CellCenter), structuredGrid->GetCellData(), nullptr, error))
  {
    return false;
  }

  return WriteZoneTimeInformation(info, error);
}

bool vtkCGNSWriter::vtkPrivate::WriteStructuredGrid(
  vtkStructuredGrid* structuredGrid, write_info& info, string& error)
{
  // get the structured grid IJK dimensions
  // and set CellDim to the correct value.
  int* dims = structuredGrid->GetDimensions();
  info.CellDim = 0;
  for (int n = 0; n < 3; n++)
  {
    if (dims[n] > 1)
    {
      info.CellDim += 1;
    }
  }

  info.BaseName = "Base";
  if (!InitCGNSFile(info, error) || !WriteBase(info, error))
  {
    return false;
  }

  info.ZoneName = "Zone 1";
  const bool rc = WriteStructuredGrid(info, structuredGrid, error);
  cg_check_operation(cg_close(info.F));
  return rc;
}

int vtkCGNSWriter::vtkPrivate::DetermineCellDimension(vtkPointSet* pointSet)
{
  int CellDim = 0;
  vtkStructuredGrid* structuredGrid = vtkStructuredGrid::SafeDownCast(pointSet);
  if (structuredGrid)
  {
    int* dims = structuredGrid->GetDimensions();
    CellDim = 0;
    for (int n = 0; n < 3; n++)
    {
      if (dims[n] > 1)
      {
        CellDim += 1;
      }
    }
  }
  else
  {
    vtkUnstructuredGrid* unstructuredGrid = vtkUnstructuredGrid::SafeDownCast(pointSet);
    if (unstructuredGrid)
    {
      CellDim = 1;
      for (vtkIdType n = 0; n < unstructuredGrid->GetNumberOfCells(); ++n)
      {
        vtkCell* cell = unstructuredGrid->GetCell(n);
        int curCellDim = cell->GetCellDimension();
        if (CellDim < curCellDim)
        {
          CellDim = curCellDim;
        }
      }
    }
  }
  return CellDim;
}

void vtkCGNSWriter::vtkPrivate::SetNameToLength(
  string& name, const vector<vtkSmartPointer<vtkDataObject>>& objects)
{
  if (name.length() > 32)
  {
    const string oldname(name);
    name = name.substr(0, 32);
    for (auto& e : objects)
    {
      int j = 1;
      const char* objectName(nullptr);
      vtkInformation* info = e->GetInformation();
      if (info && info->Has(vtkCompositeDataSet::NAME()))
      {
        objectName = info->Get(vtkCompositeDataSet::NAME());
      }
      while (objectName && objectName == name && j < 100)
      {
        name = name.substr(0, j < 10 ? 31 : 30) + to_string(j);
        ++j;
      }
      // if there are 100 duplicate zones after truncation, give up.
      // an error will be given by CGNS that a duplicate name has been found.
    }

    vtkWarningWithObjectMacro(nullptr, << "Zone name '" << oldname << "' has been truncated to '"
                                       << name
                                       << " to conform to 32-character limit on names in CGNS.");
  }
}

void vtkCGNSWriter::vtkPrivate::Flatten(vtkCompositeDataSet* composite,
  vector<vtkSmartPointer<vtkDataObject>>& o2d, vector<vtkSmartPointer<vtkDataObject>>& o3d,
  int zoneOffset, string& name)
{
  vtkPartitionedDataSet* partitioned = vtkPartitionedDataSet::SafeDownCast(composite);
  if (partitioned)
  {
    vtkNew<vtkAppendDataSets> append;
    append->SetMergePoints(true);
    if (name.length() <= 0)
    {
      name = "Zone " + to_string(zoneOffset);
    }

    for (unsigned i = 0; i < partitioned->GetNumberOfPartitions(); ++i)
    {
      vtkDataObject* partition = partitioned->GetPartitionAsDataObject(i);
      if (partition)
      {
        append->AddInputDataObject(partition);
        if (partitioned->HasMetaData(i) &&
          partitioned->GetMetaData(i)->Has(vtkCompositeDataSet::NAME()))
        {
          name = partitioned->GetMetaData(i)->Get(vtkCompositeDataSet::NAME());
        }
      }
    }
    append->Update();
    vtkDataObject* result = append->GetOutputDataObject(0);
    vtkPointSet* pointSet = vtkPointSet::SafeDownCast(result);
    if (pointSet)
    {
      pointSet->GetInformation()->Set(vtkCompositeDataSet::NAME(), name.c_str());
      const int cellDim = DetermineCellDimension(pointSet);
      if (cellDim == 3)
      {
        o3d.emplace_back(pointSet);
      }
      else
      {
        o2d.emplace_back(pointSet);
      }
    }
    return;
  }

  vtkSmartPointer<vtkDataObjectTreeIterator> iter;
  iter.TakeReference(vtkDataObjectTree::SafeDownCast(composite)->NewTreeIterator());
  iter->VisitOnlyLeavesOff();
  iter->TraverseSubTreeOff();
  iter->SkipEmptyNodesOff();
  int i(0);
  for (iter->InitTraversal(); !iter->IsDoneWithTraversal(); iter->GoToNextItem(), ++i)
  {
    name = "Zone " + to_string(i + zoneOffset);
    if (iter->HasCurrentMetaData() && iter->GetCurrentMetaData()->Has(vtkCompositeDataSet::NAME()))
    {
      name = iter->GetCurrentMetaData()->Get(vtkCompositeDataSet::NAME());
    }
    vtkDataObject* curDO = iter->GetCurrentDataObject();
    if (!curDO)
    {
      continue;
    }

    vtkCompositeDataSet* subComposite = vtkCompositeDataSet::SafeDownCast(curDO);
    if (subComposite)
    {
      Flatten(subComposite, o2d, o3d, ++zoneOffset, name);
    }
    vtkPointSet* pointSet = vtkPointSet::SafeDownCast(curDO);
    if (pointSet)
    {
      if (name.length() > 0)
      {
        pointSet->GetInformation()->Set(vtkCompositeDataSet::NAME(), name);
      }

      const int cellDim = DetermineCellDimension(pointSet);
      if (cellDim == 3)
      {
        o3d.emplace_back(pointSet);
      }
      else
      {
        o2d.emplace_back(pointSet);
      }
    }
  }
}

bool vtkCGNSWriter::vtkPrivate::WriteGridsToBase(vector<vtkSmartPointer<vtkDataObject>> blocks,
  vector<vtkSmartPointer<vtkDataObject>> otherBlocks, write_info& info, string& error)
{
  for (auto& block : blocks)
  {
    string name = block->GetInformation()->Get(vtkCompositeDataSet::NAME());
    SetNameToLength(name, blocks);
    SetNameToLength(name, otherBlocks);
    info.ZoneName = name.c_str();

    vtkStructuredGrid* structuredGrid = vtkStructuredGrid::SafeDownCast(block);
    vtkPointSet* pointSet = vtkPointSet::SafeDownCast(block);

    // test for structured grid first. it is also a vtkPointSet
    // but it needs to be written structured.
    if (structuredGrid)
    {
      if (!WriteStructuredGrid(info, structuredGrid, error))
      {
        return false;
      }
    }
    else if (pointSet)
    {
      if (!WritePointSet(info, pointSet, error))
      {
        return false;
      }
    }
    else if (block)
    {
      std::stringstream ss;
      ss << "Writing of block type '" << block->GetClassName() << "' not supported.";
      error = ss.str();
      return false;
    }
    else
    {
      vtkWarningWithObjectMacro(nullptr, << "Writing of empty block skipped.");
    }
    info.SolutionNames.clear();
  }
  return true;
}

bool vtkCGNSWriter::vtkPrivate::WriteComposite(
  write_info& info, vtkCompositeDataSet* composite, string& error)
{
  vector<vtkSmartPointer<vtkDataObject>> surfaceBlocks, volumeBlocks;
  if (composite->GetNumberOfCells() == 0 && composite->GetNumberOfPoints() == 0)
  {
    // don't write anything
    return true;
  }

  string name;
  Flatten(composite, surfaceBlocks, volumeBlocks, 0, name);

  if (!volumeBlocks.empty())
  {
    info.CellDim = 3;
    info.BaseName = "Base_Volume_Elements";
    if (!WriteBase(info, error))
    {
      return false;
    }

    if (!WriteGridsToBase(volumeBlocks, surfaceBlocks, info, error))
    {
      return false;
    }
  }

  if (!surfaceBlocks.empty())
  {
    info.CellDim = 2;
    info.BaseName = "Base_Surface_Elements";
    if (!WriteBase(info, error))
    {
      return false;
    }
    if (!WriteGridsToBase(surfaceBlocks, volumeBlocks, info, error))
    {
      return false;
    }
  }

  return true;
}

bool vtkCGNSWriter::vtkPrivate::WriteComposite(
  vtkCompositeDataSet* composite, write_info& info, string& error)
{
  if (!InitCGNSFile(info, error))
  {
    return false;
  }

  const bool rc = WriteComposite(info, composite, error);
  cg_check_operation(cg_close(info.F));
  return rc;
}

vtkStandardNewMacro(vtkCGNSWriter);

vtkCGNSWriter::vtkCGNSWriter()
{
  // use the method, this will call the corresponding library method.
  this->SetUseHDF5(this->UseHDF5);
}

void vtkCGNSWriter::SetUseHDF5(bool value)
{
  this->UseHDF5 = value;
  cg_set_file_type(value ? CG_FILE_HDF5 : CG_FILE_ADF);
}

vtkCGNSWriter::~vtkCGNSWriter()
{
  delete[] this->FileName;
  if (this->OriginalInput)
  {
    this->OriginalInput->UnRegister(this);
    this->OriginalInput = nullptr;
  }

  if (this->TimeValues)
  {
    this->TimeValues->Delete();
  }
}

void vtkCGNSWriter::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "FileName " << (this->FileName ? this->FileName : "(none)") << endl;
}

int vtkCGNSWriter::ProcessRequest(
  vtkInformation* request, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  if (request->Has(vtkDemandDrivenPipeline::REQUEST_INFORMATION()))
  {
    return this->RequestInformation(request, inputVector, outputVector);
  }
  else if (request->Has(vtkStreamingDemandDrivenPipeline::REQUEST_UPDATE_EXTENT()))
  {
    return this->RequestUpdateExtent(request, inputVector, outputVector);
  }
  // generate the data
  else if (request->Has(vtkDemandDrivenPipeline::REQUEST_DATA()))
  {
    return this->RequestData(request, inputVector, outputVector);
  }

  return this->Superclass::ProcessRequest(request, inputVector, outputVector);
}

int vtkCGNSWriter::RequestInformation(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* vtkNotUsed(outputVector))
{
  vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  if (inInfo->Has(vtkStreamingDemandDrivenPipeline::TIME_STEPS()))
  {
    this->NumberOfTimeSteps = inInfo->Length(vtkStreamingDemandDrivenPipeline::TIME_STEPS());
  }
  else
  {
    this->NumberOfTimeSteps = 0;
  }

  return 1;
}

int vtkCGNSWriter::RequestUpdateExtent(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* vtkNotUsed(outputVector))
{
  if (!this->TimeValues)
  {
    this->TimeValues = vtkDoubleArray::New();
    vtkInformation* info = inputVector[0]->GetInformationObject(0);
    double* data = info->Get(vtkStreamingDemandDrivenPipeline::TIME_STEPS());
    int len = info->Length(vtkStreamingDemandDrivenPipeline::TIME_STEPS());
    this->TimeValues->SetNumberOfValues(len);
    if (data)
    {
      for (int i = 0; i < len; i++)
      {
        this->TimeValues->SetValue(i, data[i]);
      }
    }
  }
  if (this->TimeValues && this->WriteAllTimeSteps)
  {
    if (this->TimeValues->GetPointer(0))
    {
      double timeReq;
      timeReq = this->TimeValues->GetValue(this->CurrentTimeIndex);
      inputVector[0]->GetInformationObject(0)->Set(
        vtkStreamingDemandDrivenPipeline::UPDATE_TIME_STEP(), timeReq);
    }
  }

  return 1;
}

int vtkCGNSWriter::FillInputPortInformation(int vtkNotUsed(port), vtkInformation* info)
{
  info->Remove(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE());
  info->Append(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
  info->Append(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkCompositeDataSet");
  return 1;
}

int vtkCGNSWriter::RequestData(vtkInformation* request, vtkInformationVector** inputVector,
  vtkInformationVector* vtkNotUsed(outputVector))
{
  if (!this->FileName)
  {
    return 1;
  }

  vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  if (this->OriginalInput)
  {
    this->OriginalInput->UnRegister(this);
  }

  this->OriginalInput = vtkDataObject::SafeDownCast(inInfo->Get(vtkDataObject::DATA_OBJECT()));
  if (this->OriginalInput)
  {
    this->OriginalInput->Register(this);
  }

  // is this the first request
  if (this->CurrentTimeIndex == 0 && this->WriteAllTimeSteps)
  {
    // Tell the pipeline to start looping.
    request->Set(vtkStreamingDemandDrivenPipeline::CONTINUE_EXECUTING(), 1);
  }

  this->WriteData();
  if (!this->WasWritingSuccessful)
    SetErrorCode(1L);

  this->CurrentTimeIndex++;
  if (this->CurrentTimeIndex >= this->NumberOfTimeSteps)
  {
    this->CurrentTimeIndex = 0;
    if (this->WriteAllTimeSteps)
    {
      // Tell the pipeline to stop looping.
      request->Set(vtkStreamingDemandDrivenPipeline::CONTINUE_EXECUTING(), 0);
    }
  }

  return this->WasWritingSuccessful ? 1 : 0;
}

void vtkCGNSWriter::WriteData()
{
  this->WasWritingSuccessful = false;
  if (!this->FileName || !this->OriginalInput)
    return;

  write_info info;
  info.FileName = this->FileName;
  info.WriteAllTimeSteps = this->WriteAllTimeSteps;
  if (this->TimeValues && this->CurrentTimeIndex < this->TimeValues->GetNumberOfValues())
  {
    info.TimeStep = this->TimeValues->GetValue(this->CurrentTimeIndex);
  }
  else
  {
    info.TimeStep = 0.0;
  }

  string error;
  if (this->OriginalInput->IsA("vtkCompositeDataSet"))
  {
    vtkCompositeDataSet* composite = vtkCompositeDataSet::SafeDownCast(this->OriginalInput);
    this->WasWritingSuccessful = vtkCGNSWriter::vtkPrivate::WriteComposite(composite, info, error);
  }
  else if (this->OriginalInput->IsA("vtkDataSet"))
  {
    if (this->OriginalInput->IsA("vtkStructuredGrid"))
    {
      vtkStructuredGrid* structuredGrid = vtkStructuredGrid::SafeDownCast(this->OriginalInput);
      this->WasWritingSuccessful =
        vtkCGNSWriter::vtkPrivate::WriteStructuredGrid(structuredGrid, info, error);
    }
    else if (this->OriginalInput->IsA("vtkPointSet"))
    {
      vtkPointSet* unstructuredGrid = vtkPointSet::SafeDownCast(this->OriginalInput);
      this->WasWritingSuccessful =
        vtkCGNSWriter::vtkPrivate::WritePointSet(unstructuredGrid, info, error);
    }
    else
    {
      error = string("Unsupported class type '") + this->OriginalInput->GetClassName() +
        "' on input.\nSupported types are vtkStructuredGrid, vtkPointSet, their subclasses and "
        "multi-block datasets of said classes.";
    }
  }
  else
  {
    vtkErrorMacro(<< "Unsupported class type '" << this->OriginalInput->GetClassName()
                  << "' on input.\nSupported types are vtkStructuredGrid, vtkPointSet, their "
                     "subclasses and multi-block datasets of said classes.");
  }

  // the writer can be used for multiple timesteps
  // and the array is re-created at each use.
  // except when writing multiple timesteps
  if (!this->WriteAllTimeSteps && this->TimeValues)
  {
    this->TimeValues->Delete();
    this->TimeValues = nullptr;
  }

  if (!this->WasWritingSuccessful)
  {
    vtkErrorMacro(<< " Writing failed: " << error);
  }
}
