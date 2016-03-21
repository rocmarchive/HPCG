
//@HEADER
// ***************************************************
//
// HPCG: High Performance Conjugate Gradient Benchmark
//
// Contact:
// Michael A. Heroux ( maherou@sandia.gov)
// Jack Dongarra     (dongarra@eecs.utk.edu)
// Piotr Luszczek    (luszczek@eecs.utk.edu)
//
// ***************************************************
//@HEADER

/*!
 @file OptimizeProblem.cpp

 HPCG routine
 */

#include "OptimizeProblem.hpp"
#include <iostream>
using namespace std;
int row ;
/*!
  Optimizes the data structures used for CG iteration to increase the
  performance of the benchmark version of the preconditioned CG algorithm.

  @param[inout] A      The known system matrix, also contains the MG hierarchy in attributes Ac and mgData.
  @param[inout] A_ref  The reference which needs to be reordered accordingly.
  @param[inout] colors The vecotor to store the index of the reordering order.

  @return returns 0 upon success and non-zero otherwise

  @see GenerateGeometry
  @see GenerateProblem
*/

// free the reference matrix
void free_refmatrix_m(SparseMatrix &A) {
  for (int i = 0 ; i < A.localNumberOfRows; i++) {
    delete [] A.matrixValues[i];
    delete [] A.mtxIndL[i];
  }
}

// Hash fucntion

int hash_function(int index , int nnz) {
  int i;
  unsigned int uiHash = 0U;
  uiHash = (index * (unsigned int) row) + nnz;
  return uiHash;
}


// Copy source to destination

void copy_value(std::vector<local_int_t> &dest,  std::vector<local_int_t> &source) {
  for (int i = 0; i < row; ++i) {
    dest[i] = source[i];
  }
}

// luby's graph coloring algorthim - nvidia's approach

void lubys_graph_coloring(
  int c,
  int *row_offset,
  int *col_index,
  std::vector<local_int_t> &colors,
  int *random,
  std::vector<local_int_t> &temp) {

  //copy_value(temp,colors);
  std::copy(colors.begin(), colors.end(), temp.begin());
  for (int i = 0; i < row; i++) {
    int flag = 1;
    if (temp[i] != -1) {
      continue;
    }
    int ir = random[i];
    for (int k = row_offset[i]; k < row_offset[i + 1]; k++) {
      int j = col_index[k];
      int jc = colors[j];
      if (((jc != -1) && (jc != c)) || (i == j)) {
        continue;
      }
      int jr = random[j];
      if (ir <= jr) {
        flag = 0;
      }
    }
    if (flag) {
      temp[i] = c;
    }
  }
  //copy_value(colors,temp);
  std::copy(temp.begin(), temp.end(), colors.begin());
}


int OptimizeProblem(const SparseMatrix &A, SparseMatrix &A_ref) {

  const local_int_t nrow = A.localNumberOfRows;
  row = nrow;
  int *random = new int [nrow];
  std::vector<local_int_t> temp(nrow, -1);
  int *row_offset, *col_index;
  col_index = new int [nrow * 27];
  row_offset = new int [(nrow + 1)];
  row_offset[0] = 0;

  // Initialize local Color array and random array using hash functions.
  // Save the mtxIndL in a single dimensional array for column index reference.
  for (int k = 0, i = 0; i < nrow; i++) {
    random[i] = (i * (unsigned int)row) + A.nonzerosInRow[i];
    for (int j = 0; j < A.nonzerosInRow[i]; j++) {
      col_index[k] = A.mtxIndL[i][j];
      k++;
    }
  }

  // Calculate the row offset.
  int ridx = 1;
  int sum = 0;
  for (int i = 0; i < nrow; i++) {
    sum =  sum + A.nonzerosInRow[i];
    row_offset[ridx] = sum;
    ridx++;
  }

  // Call luby's graph coloring algorithm.
  int c = 0;
  for (c = 0; c < nrow; c++) {

    lubys_graph_coloring(c, row_offset, col_index, A_ref.colors, random, temp);
    int left = std::count(A_ref.colors.begin(), A_ref.colors.end(), -1);
    if (left == 0) {
      break;
    }
  }

  // Calculate number of rows with the same color and save it in counter vector.
  std::vector<local_int_t> counters(c + 1);
  A_ref.counters.resize(c + 1);
  std::fill(counters.begin(), counters.end(), 0);
  for (local_int_t i = 0; i < nrow; ++i) {
    counters[A_ref.colors[i]]++;
  }

  // Calculate color offset using counter vector.
  local_int_t old = 0 , old0 = 0;
  for (int i = 1; i <= c; ++i) {
    old0 = counters[i];
    counters[i] = counters[i - 1] + old;
    old = old0;
  }
  counters[0] = 0;


  for (int i = 0; i <= c; ++i) {
    A_ref.counters[i] = counters[i];
  }

  // translate `colors' into a permutation.
  for (local_int_t i = 0; i < nrow; ++i) { // for each color `c'
    A_ref.colors[i] = counters[A_ref.colors[i]]++;
  }

  // Rearranges the reference matrix according to the coloring index.
  for (int i = 0; i < nrow; i++) {

    const int currentNumberOfNonzeros = A.nonzerosInRow[A_ref.colors[i]];
    A_ref.nonzerosInRow[i] = A.nonzerosInRow[A_ref.colors[i]];
    const double *const currentValues = A.matrixValues[A_ref.colors[i]];
    const local_int_t *const currentColIndices = A.mtxIndL[A_ref.colors[i]];

    double *diagonalValue = A.matrixDiagonal[A_ref.colors[i]];
    A_ref.matrixDiagonal[i] = diagonalValue;

    //rearrange the elements in the row
    int col_indx = 0;
    for (int k = 0; k < nrow; k++) {
      for (int j = 0; j < currentNumberOfNonzeros; j++) {
        if (A_ref.colors[k] == currentColIndices[j]) {
          A_ref.matrixValues[i][col_indx] = currentValues[j];
          A_ref.mtxIndL[i][col_indx++] = k;
          break;
        }
      }
    }
  }

  delete [] row_offset;
  delete [] col_index;
  delete [] random;

  return 0;
}

// Helper function (see OptimizeProblem.hpp for details)
double OptimizeProblemMemoryUse(const SparseMatrix &A) {
  return 0.0;
}
