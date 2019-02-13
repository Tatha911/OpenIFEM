#ifndef SHARED_HYPERELASTIC_SOLVER
#define SHARED_HYPERELASTIC_SOLVER

#include <deal.II/base/symmetric_tensor.h>
#include <deal.II/base/tensor.h>
#include <deal.II/fe/mapping_q_eulerian.h>
#include <deal.II/lac/linear_operator.h>
#include <deal.II/lac/packaged_operation.h>
#include <deal.II/physics/elasticity/kinematics.h>
#include <deal.II/physics/elasticity/standard_tensors.h>

#include <mfree_iwf/body.h>
#include <mfree_iwf/cont_mech.h>
#include <mfree_iwf/derivatives.h>
#include <mfree_iwf/material.h>
#include <mfree_iwf/neighbor_search.h>
#include <mfree_iwf/particle.h>
#include <mfree_iwf/vtk_writer.h>

#include "mpi_shared_solid_solver.h"

namespace Solid
{
  namespace MPI
  {
    using namespace dealii;

    extern template class SharedSolidSolver<2>;
    extern template class SharedSolidSolver<3>;

    template <int dim>
    class SharedHypoElasticity : public SharedSolidSolver<dim>
    {
    public:
      SharedHypoElasticity(Triangulation<dim> &,
                           const Parameters::AllParameters &,
                           double dx,
                           double hdx);
      ~SharedHypoElasticity() {}

    private:
      using SharedSolidSolver<dim>::triangulation;
      using SharedSolidSolver<dim>::parameters;
      using SharedSolidSolver<dim>::dof_handler;
      using SharedSolidSolver<dim>::scalar_dof_handler;
      using SharedSolidSolver<dim>::fe;
      using SharedSolidSolver<dim>::scalar_fe;
      using SharedSolidSolver<dim>::volume_quad_formula;
      using SharedSolidSolver<dim>::face_quad_formula;
      using SharedSolidSolver<dim>::constraints;
      using SharedSolidSolver<dim>::system_matrix;
      using SharedSolidSolver<dim>::mass_matrix;
      using SharedSolidSolver<dim>::system_rhs;
      using SharedSolidSolver<dim>::current_acceleration;
      using SharedSolidSolver<dim>::current_velocity;
      using SharedSolidSolver<dim>::current_displacement;
      using SharedSolidSolver<dim>::previous_acceleration;
      using SharedSolidSolver<dim>::previous_velocity;
      using SharedSolidSolver<dim>::previous_displacement;
      using SharedSolidSolver<dim>::strain;
      using SharedSolidSolver<dim>::stress;
      using SharedSolidSolver<dim>::mpi_communicator;
      using SharedSolidSolver<dim>::n_mpi_processes;
      using SharedSolidSolver<dim>::this_mpi_process;
      using SharedSolidSolver<dim>::pcout;
      using SharedSolidSolver<dim>::time;
      using SharedSolidSolver<dim>::timer;
      using SharedSolidSolver<dim>::locally_owned_dofs;
      using SharedSolidSolver<dim>::locally_owned_scalar_dofs;
      using SharedSolidSolver<dim>::locally_relevant_dofs;
      using SharedSolidSolver<dim>::times_and_names;
      using SharedSolidSolver<dim>::cell_property;

      void initialize_system() override;

      virtual void update_strain_and_stress() override;

      /** Assemble the lhs and rhs at the same time. */
      void assemble_system(bool);

      /// Run one time step.
      void run_one_step(bool);

      body<particle_tl_weak> m_body;

      std::vector<int> vertex_mapping;

      void construct_particles();

      void synchronize();

      double dx;

      double hdx;

      Vector<double> serialized_displacement;

      Vector<double> serialized_velocity;
    };
  } // namespace MPI
} // namespace Solid

#endif