#ifndef INS_IMEX
#define INS_IMEX

#include <deal.II/base/function.h>
#include <deal.II/base/logstream.h>
#include <deal.II/base/quadrature_lib.h>
#include <deal.II/base/quadrature_point_data.h>
#include <deal.II/base/tensor.h>
#include <deal.II/base/timer.h>
#include <deal.II/base/utilities.h>

#include <deal.II/lac/block_sparse_matrix.h>
#include <deal.II/lac/block_vector.h>
#include <deal.II/lac/constraint_matrix.h>
#include <deal.II/lac/dynamic_sparsity_pattern.h>
#include <deal.II/lac/full_matrix.h>
#include <deal.II/lac/precondition.h>
#include <deal.II/lac/solver_cg.h>
#include <deal.II/lac/solver_gmres.h>
#include <deal.II/lac/sparse_direct.h>

#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/grid_refinement.h>
#include <deal.II/grid/grid_tools.h>
#include <deal.II/grid/manifold_lib.h>
#include <deal.II/grid/tria.h>
#include <deal.II/grid/tria_accessor.h>
#include <deal.II/grid/tria_iterator.h>

#include <deal.II/dofs/dof_accessor.h>
#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_renumbering.h>
#include <deal.II/dofs/dof_tools.h>

#include <deal.II/fe/fe_q.h>
#include <deal.II/fe/fe_system.h>
#include <deal.II/fe/fe_values.h>

#include <deal.II/numerics/data_out.h>
#include <deal.II/numerics/error_estimator.h>
#include <deal.II/numerics/matrix_tools.h>
#include <deal.II/numerics/vector_tools.h>

// To transfer solutions between meshes, this file is included:
#include <deal.II/numerics/solution_transfer.h>

// This file includes UMFPACK: the direct solver:
#include <deal.II/lac/sparse_direct.h>

// And the one for ILU preconditioner:
#include <deal.II/lac/sparse_ilu.h>

#include <fstream>
#include <iostream>
#include <sstream>

#include "parameters.h"
#include "utilities.h"

template <int>
class FSI;

namespace Fluid
{
  using namespace dealii;

  /** \brief Incompressible Navier Stokes equation solver
    *        using implicit-explicit time scheme.
    *
    * This program is built upon dealii tutorials step-57, step-22, step-20.
    * Although the density does not matter in the incompressible flow, we
    * still include it in the formulation in order to be consistent with the
    * slightly compressible flow. Correspondingly the viscosity represents
    * dynamic visocity \f$\mu\f$ instead of kinetic visocity \f$\nu\f$,
    * and the pressure block in the solution is the non-normalized pressure.
    *
    * The system equation is written in the incremental form, and we treat
    * the convection term explicitly. Therefore the system equation is linear
    * and symmetric, which does not need to be solved with Newton's iteration.
    * The system is further stablized and preconditioned with Grad-Div method,
    * where GMRES solver is used as the outer solver.
    */
  template <int dim>
  class InsIMEX
  {
  public:
    //! FSI solver need access to the private members of this solver.
    friend FSI<dim>;

    //! Constructor
    InsIMEX(Triangulation<dim> &, const Parameters::AllParameters &);

    //! Run the simulation
    void run();

    //! Return the solution for testing.
    BlockVector<double> get_current_solution() const;

  private:
    class BoundaryValues;
    class BlockSchurPreconditioner;
    struct CellProperty;

    //! Set up the dofs based on the finite element and renumber them.
    void setup_dofs();

    //! Set up the nonzero and zero constraints.
    void make_constraints();

    //! Initialize the cell properties, which only matters in FSI applications.
    void setup_cell_property();

    /// Specify the sparsity pattern and reinit matrices and vectors based on
    /// the dofs and constraints.
    void initialize_system();

    /*! \brief Assemble the system matrix, mass mass matrix, and the RHS.
     *
     *  It can be used to assemble the entire system or only the RHS.
     *  An additional option is added to determine whether nonzero
     *  constraints or zero constraints should be used.
     */
    void assemble(bool use_nonzero_constraints, bool assemble_system);

    /// Mesh adaption.
    void refine_mesh(const unsigned int, const unsigned int);

    /// Output in vtu format.
    void output_results(const unsigned int) const;

    /*! \brief Solve the linear system using FGMRES solver plus block
     *         preconditioner.
     *
     *  After solving the linear system, the same ConstraintMatrix as used
     *  in assembly must be used again, to set the constrained value.
     *  The second argument is used to determine
     *  whether the block preconditioner should be reset or not.
     */
    std::pair<unsigned int, double> solve(bool use_nonzero_constraints,
                                          bool assemble_system);

    /// Run the simulation for one time step.
    void run_one_step();

    double viscosity; //!< Dynamic viscosity
    double rho;
    double gamma;
    const unsigned int degree;
    std::vector<types::global_dof_index> dofs_per_block;

    Triangulation<dim> &triangulation;
    FESystem<dim> fe;
    DoFHandler<dim> dof_handler;
    QGauss<dim> volume_quad_formula;
    QGauss<dim - 1> face_quad_formula;

    ConstraintMatrix zero_constraints;
    ConstraintMatrix nonzero_constraints;

    BlockSparsityPattern sparsity_pattern;
    BlockSparseMatrix<double> system_matrix;
    BlockSparseMatrix<double> mass_matrix;
    SparsityPattern mass_schur_pattern;
    SparseMatrix<double> mass_schur;

    /// The latest known solution.
    BlockVector<double> present_solution;
    /// The increment at a certain time step.
    BlockVector<double> solution_increment;
    BlockVector<double> system_rhs;

    const double tolerance;
    const unsigned int max_iteration;

    Utils::Time time;
    mutable TimerOutput timer;

    Parameters::AllParameters parameters;

    /// The BlockSchurPreconditioner for the entire system.
    std::shared_ptr<BlockSchurPreconditioner> preconditioner;

    CellDataStorage<typename Triangulation<dim>::cell_iterator, CellProperty>
      cell_property;

    /*! \brief Helper class to specify space/time-dependent Dirichlet BCs,
     *         as the input file can only handle constant BC values.
     *
     *  It specifies a parabolic velocity profile at the left side boundary,
     *  and all the remaining boundaries are considered as walls
     *  except for the right side one.
     */
    class BoundaryValues : public Function<dim>
    {
    public:
      BoundaryValues() : Function<dim>(dim + 1) {}
      virtual double value(const Point<dim> &p,
                           const unsigned int component) const;

      virtual void vector_value(const Point<dim> &p,
                                Vector<double> &values) const;
    };

    /** \brief Block preconditioner for the system
      *
      * A right block preconditioner is defined here:
      * \f{eqnarray*}{
      *      P^{-1} = \begin{pmatrix} \tilde{A}^{-1} & 0\\ 0 & I\end{pmatrix}
      *               \begin{pmatrix} I & -B^T\\ 0 & I\end{pmatrix}
      *               \begin{pmatrix} I & 0\\ 0 & \tilde{S}^{-1}\end{pmatrix}
      * \f}
      *
      * \f$\tilde{A}\f$ is symmetric since the convection term is eliminated
      * from the LHS.
      *
      * \f$\tilde{S}^{-1}\f$ is the inverse of the total Schur complement,
      * which consists of a reaction term, a diffusion term, a Grad-Div term
      * and a convection term.
      * In practice, the convection contribution is ignored because it is not
      * clear how to treat it. But the block preconditioner is good enough even
      * without it. Namely,
      *
      * \f[
      *   \tilde{S}^{-1} = -(\nu + \gamma)M_p^{-1} -
      *   \frac{1}{\Delta{t}}{[B(diag(M_u))^{-1}B^T]}^{-1}
      * \f]
      * where \f$M_p\f$ is the pressure mass, and
      * \f${[B(diag(M_u))^{-1}B^T]}\f$
      * is an approximation to the Schur complement of (velocity) mass matrix
      * \f$BM_u^{-1}B^T\f$.
      *
      * In summary, in order to form the BlockSchurPreconditioner for our
      * system, we need to compute \f$M_u^{-1}\f$, \f$M_p^{-1}\f$,
      * \f$\tilde{A}^{-1}\f$, and then operate on them.
      * These matrices are all symmetric in IMEX scheme.
      */
    class BlockSchurPreconditioner : public Subscriptor
    {
    public:
      /// Constructor.
      BlockSchurPreconditioner(TimerOutput &timer,
                               double gamma,
                               double viscosity,
                               double rho,
                               double dt,
                               const BlockSparseMatrix<double> &system,
                               const BlockSparseMatrix<double> &mass,
                               SparseMatrix<double> &schur);

      /// The matrix-vector multiplication must be defined.
      void vmult(BlockVector<double> &dst,
                 const BlockVector<double> &src) const;

    private:
      TimerOutput &timer;
      const double gamma;
      const double viscosity;
      const double rho;
      const double dt;

      /// dealii smart pointer checks if an object is still being referenced
      /// when it is destructed therefore is safer than plain reference.
      const SmartPointer<const BlockSparseMatrix<double>> system_matrix;
      const SmartPointer<const BlockSparseMatrix<double>> mass_matrix;
      /**
       * As discussed, \f${[B(diag(M_u))^{-1}B^T]}\f$ and its inverse
       * need to be computed.
       * We can either explicitly compute it out as a matrix, or define it as a
       * class
       * with a vmult operation. The second approach saves some computation to
       * construct the matrix, but leads to slow convergence in CG solver
       * because
       * of the absence of preconditioner.
       * Based on my tests, the first approach is more than 10 times faster so I
       * go with this route.
       */
      const SmartPointer<SparseMatrix<double>> mass_schur;
    };

    /// A data structure that caches the real/artificial fluid indicator,
    /// FSI stress, and FSI acceleration terms at quadrature points, that
    /// will only be used in FSI simulations.
    struct CellProperty
    {
      int indicator; //!< Domain indicator: 1 for artificial fluid 0 for real
                     //! fluid.
      Tensor<1, dim> fsi_acceleration; //!< The acceleration term in FSI force.
      SymmetricTensor<2, dim> fsi_stress; //!< The stress term in FSI force.
    };
  };
}

#endif